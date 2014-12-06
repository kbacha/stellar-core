#include "Peer.h"
#include "lib/util/Logging.h"
#include "main/Application.h"
#include "generated/stellar.hh"
#include "xdrpp/marshal.h"
#include "overlay/PeerMaster.h"

#define MS_TO_WAIT_FOR_HELLO 2000

// LATER: need to add some way of docking peers that are misbehaving by sending
// you bad data

namespace stellar
{
Peer::Peer(Application &app, PeerRole role)
    : mApp(app),
      mRole(role),
      mState(role == ACCEPTOR ? CONNECTED : CONNECTING),
      mRemoteListeningPort(-1)
{
    if (mRole == ACCEPTOR)
    {
        // Schedule a 'say hello' event at the next opportunity,
        // if we're the acceptor-role.
        mApp.getMainIOService().post([this]()
                                     {
                                         this->sendHello();
                                     });
    }
}

void
Peer::sendHello()
{
    stellarxdr::StellarMessage msg;
    msg.type(stellarxdr::HELLO);
    msg.hello().protocolVersion = mApp.mConfig.PROTOCOL_VERSION;
    msg.hello().versionStr = mApp.mConfig.VERSION_STR;

    sendMessage(msg);
}

void
Peer::connectHandler(const asio::error_code &error)
{
    if (error)
    {
        CLOG(WARNING, "Overlay") << "connectHandler error: " << error;
        drop();
    }
    else
    {
        mState = CONNECTED;
        sendHello();
    }
}

void
Peer::sendDontHave(stellarxdr::MessageType type, stellarxdr::uint256 &itemID)
{
    stellarxdr::StellarMessage msg;
    msg.type(stellarxdr::DONT_HAVE);
    msg.dontHave().reqHash = itemID;
    msg.dontHave().type = type;

    sendMessage(msg);
}

void
Peer::sendQuorumSet(QuorumSet::pointer qSet)
{
    stellarxdr::StellarMessage msg;
    msg.type(stellarxdr::QUORUMSET);
    qSet->toXDR(msg.quorumSet());

    sendMessage(msg);
}
void
Peer::sendGetTxSet(stellarxdr::uint256 &setID)
{
    stellarxdr::StellarMessage newMsg;
    newMsg.type(stellarxdr::GET_TX_SET);
    newMsg.txSetHash() = setID;

    sendMessage(newMsg);
}
void
Peer::sendGetQuorumSet(stellarxdr::uint256 &setID)
{
    stellarxdr::StellarMessage newMsg;
    newMsg.type(stellarxdr::GET_QUORUMSET);
    newMsg.txSetHash() = setID;

    sendMessage(newMsg);
}

void
Peer::sendPeers()
{
    // LATER
}

void
Peer::sendMessage(stellarxdr::StellarMessage msg)
{
    CLOG(TRACE, "Overlay") << "sending stellarMessage";
    xdr::msg_ptr xdrBytes(xdr::xdr_to_msg(msg));
    this->sendMessage(std::move(xdrBytes));
}

void
Peer::recvMessage(xdr::msg_ptr const &msg)
{
    CLOG(TRACE, "Overlay") << "received xdr::msg_ptr";
    StellarMessagePtr stellarMsg =
        std::make_shared<stellarxdr::StellarMessage>();
    xdr::xdr_from_msg(msg, *stellarMsg.get());
    recvMessage(stellarMsg);
}

void
Peer::recvMessage(StellarMessagePtr stellarMsg)
{
    CLOG(TRACE, "Overlay") << "recv: " << stellarMsg->type();

    if (mState < GOT_HELLO && stellarMsg->type() != stellarxdr::HELLO)
    {
        CLOG(WARNING, "Overlay") << "recv: " << stellarMsg->type()
                                 << " before hello";
        drop();
        return;
    }

    switch (stellarMsg->type())
    {
    case stellarxdr::ERROR_MSG:
    {
        recvError(stellarMsg);
    }
    break;

    case stellarxdr::HELLO:
    {
        this->recvHello(stellarMsg);
    }
    break;

    case stellarxdr::DONT_HAVE:
    {
        recvDontHave(stellarMsg);
    }
    break;

    case stellarxdr::GET_PEERS:
    {
        recvGetPeers(stellarMsg);
    }
    break;

    case stellarxdr::PEERS:
    {
        recvPeers(stellarMsg);
    }
    break;

    case stellarxdr::GET_HISTORY:
    {
        recvGetHistory(stellarMsg);
    }
    break;

    case stellarxdr::HISTORY:
    {
        recvHistory(stellarMsg);
    }
    break;

    case stellarxdr::GET_DELTA:
    {
        recvGetDelta(stellarMsg);
    }
    break;

    case stellarxdr::DELTA:
    {
        recvDelta(stellarMsg);
    }
    break;

    case stellarxdr::GET_TX_SET:
    {
        recvGetTxSet(stellarMsg);
    }
    break;

    case stellarxdr::TX_SET:
    {
        recvTxSet(stellarMsg);
    }
    break;

    case stellarxdr::GET_VALIDATIONS:
    {
        recvGetValidations(stellarMsg);
    }
    break;

    case stellarxdr::VALIDATIONS:
    {
        recvValidations(stellarMsg);
    }
    break;

    case stellarxdr::TRANSACTION:
    {
        recvTransaction(stellarMsg);
    }
    break;

    case stellarxdr::GET_QUORUMSET:
    {
        recvGetQuorumSet(stellarMsg);
    }
    break;

    case stellarxdr::QUORUMSET:
    {
        recvQuorumSet(stellarMsg);
    }
    break;

    case stellarxdr::FBA_MESSAGE:
    {
        recvFBAMessage(stellarMsg);
    }
    break;
    case stellarxdr::JSON_TRANSACTION:
    {
        assert(false);
    }
    break;
    }
}

void
Peer::recvGetDelta(StellarMessagePtr msg)
{
    // LATER
}
void
Peer::recvDelta(StellarMessagePtr msg)
{
    // LATER
}

void
Peer::recvDontHave(StellarMessagePtr msg)
{
    switch (msg->dontHave().type)
    {
    case stellarxdr::HISTORY:
        // LATER
        break;
    case stellarxdr::DELTA:
        // LATER
        break;
    case stellarxdr::TX_SET:
        mApp.getTxHerderGateway().doesntHaveTxSet(msg->dontHave().reqHash,
                                                  shared_from_this());
        break;
    case stellarxdr::QUORUMSET:
        mApp.getOverlayGateway().doesntHaveQSet(msg->dontHave().reqHash,
                                                shared_from_this());
        break;
    case stellarxdr::VALIDATIONS:
    default:
        break;
    }
}

void
Peer::recvGetTxSet(StellarMessagePtr msg)
{
    TransactionSet::pointer txSet =
        mApp.getTxHerderGateway().fetchTxSet(msg->txSetHash(), false);
    if (txSet)
    {
        stellarxdr::StellarMessage newMsg;
        newMsg.type(stellarxdr::TX_SET);
        txSet->toXDR(newMsg.txSet());

        sendMessage(newMsg);
    }
    else
    {
        sendDontHave(stellarxdr::TX_SET, msg->txSetHash());
    }
}
void
Peer::recvTxSet(StellarMessagePtr msg)
{
    TransactionSet::pointer txSet =
        std::make_shared<TransactionSet>(msg->txSet());
    mApp.getTxHerderGateway().recvTransactionSet(txSet);
}

void
Peer::recvTransaction(StellarMessagePtr msg)
{
    Transaction::pointer transaction =
        Transaction::makeTransactionFromWire(msg->transaction());
    if (transaction)
    {
        if (mApp.getTxHerderGateway().recvTransaction(
                transaction)) // add it to our current set
        {
            mApp.getOverlayGateway().broadcastMessage(msg, shared_from_this());
        }
    }
}

void
Peer::recvGetQuorumSet(StellarMessagePtr msg)
{
    QuorumSet::pointer qset =
        mApp.getOverlayGateway().fetchQuorumSet(msg->qSetHash(), false);
    if (qset)
    {
        sendQuorumSet(qset);
    }
    else
    {
        sendDontHave(stellarxdr::QUORUMSET, msg->qSetHash());
        // do we want to ask other people for it?
    }
}
void
Peer::recvQuorumSet(StellarMessagePtr msg)
{
    QuorumSet::pointer qset =
        std::make_shared<QuorumSet>(msg->quorumSet(), mApp);
    mApp.getOverlayGateway().recvQuorumSet(qset);
}

void
Peer::recvFBAMessage(StellarMessagePtr msg)
{
    stellarxdr::FBAEnvelope envelope = msg->fbaMessage();
    Statement::pointer statement = Statement::makeStatement(envelope);

    mApp.getOverlayGateway().recvFloodedMsg(statement->mSignature, msg,
                                            statement->getLedgerIndex(),
                                            shared_from_this());
    mApp.getFBAGateway().recvStatement(statement);
}

void
Peer::recvError(StellarMessagePtr msg)
{
    // LATER
}
void
Peer::recvHello(StellarMessagePtr msg)
{
    mRemoteProtocolVersion = msg->hello().protocolVersion;
    mRemoteVersion = msg->hello().versionStr;
    mRemoteListeningPort = msg->hello().port;
    CLOG(INFO, "Overlay") << "recvHello: " << mRemoteProtocolVersion << " "
                          << mRemoteVersion << " " << mRemoteListeningPort;
    mState = GOT_HELLO;
}
void
Peer::recvGetPeers(StellarMessagePtr msg)
{
    // LATER
}
void
Peer::recvPeers(StellarMessagePtr msg)
{
    // LATER
}
void
Peer::recvGetHistory(StellarMessagePtr msg)
{
    // LATER
}
void
Peer::recvHistory(StellarMessagePtr msg)
{
    // LATER
}

void
Peer::recvGetValidations(StellarMessagePtr msg)
{
    // LATER
}
void
Peer::recvValidations(StellarMessagePtr msg)
{
    // LATER
}

///////////////////////////////////////////////////////////////////////
// TCPPeer
///////////////////////////////////////////////////////////////////////

const char *TCPPeer::kSQLCreateStatement =
    "CREATE TABLE IF NOT EXISTS Peers (                      \
        peerID      INT PRIMARY KEY AUTO_INCREMENT, \
        ip          varchar(16),            \
        port        INT,                \
        lastTry     timestamp,          \
        lastConnect timestamp,      \
        rank    INT     \
    );";

TCPPeer::TCPPeer(Application &app, shared_ptr<asio::ip::tcp::socket> socket,
                 PeerRole role)
    : Peer(app, role), mSocket(socket), mHelloTimer(app.getMainIOService())
{
    mHelloTimer.expires_from_now(
        std::chrono::milliseconds(MS_TO_WAIT_FOR_HELLO));
    mHelloTimer.async_wait(
        [socket](asio::error_code const &ec)
        {
            socket->shutdown(asio::socket_base::shutdown_both);
            socket->close();
        });
}

std::string
TCPPeer::getIP()
{
    return mSocket->remote_endpoint().address().to_string();
}

void
TCPPeer::connect()
{
    // GRAYDON mSocket->async_connect(server_endpoint, your_completion_handler);
}

void
TCPPeer::sendMessage(xdr::msg_ptr &&xdrBytes)
{
    using std::placeholders::_1;
    using std::placeholders::_2;

    // Pass ownership of a serizlied XDR message buffer, along with an
    // asio::buffer pointing into it, to the callback for async_write, so it
    // survives as long as the request is in flight in the io_service, and
    // is deallocated when the write completes.
    //
    // The messy bind-of-lambda expression is required due to C++11 not
    // supporting moving (passing ownership) into a lambda capture. This is
    // fixed in C++14 but we're not there yet.

    auto self = shared_from_this();
    asio::async_write(*(mSocket.get()),
                      asio::buffer(xdrBytes->raw_data(), xdrBytes->raw_size()),
                      std::bind(
                          [self](asio::error_code const &ec, std::size_t length,
                                 xdr::msg_ptr const &)
                          {
                              self->writeHandler(ec, length);
                          },
                          _1, _2, std::move(xdrBytes)));
}

void
TCPPeer::writeHandler(const asio::error_code &error,
                      std::size_t bytes_transferred)
{
    if (error)
    {
        CLOG(WARNING, "Overlay") << "writeHandler error: " << error;
        // LATER drop Peer
    }
}

void
TCPPeer::startRead()
{
    auto self = shared_from_this();
    asio::async_read(*(mSocket.get()), asio::buffer(mIncomingHeader),
                     [self](std::error_code ec, std::size_t length)
                     {
        self->Peer::readHeaderHandler(ec, length);
    });
}

int
TCPPeer::getIncomingMsgLength()
{
    int length = mIncomingHeader[0];
    length <<= 8;
    length |= mIncomingHeader[1];
    length <<= 8;
    length |= mIncomingHeader[2];
    length <<= 8;
    length |= mIncomingHeader[3];
    return (length);
}

void
TCPPeer::readHeaderHandler(const asio::error_code &error,
                           std::size_t bytes_transferred)
{
    if (!error)
    {
        mIncomingBody.resize(getIncomingMsgLength());
        auto self = shared_from_this();
        asio::async_read(*mSocket.get(), asio::buffer(mIncomingBody),
                         [self](std::error_code ec, std::size_t length)
                         {
            self->Peer::readBodyHandler(ec, length);
        });
    }
    else
    {
        CLOG(WARNING, "Overlay") << "readHeaderHandler error: " << error;
        // LATER drop Peer
    }
}

void
TCPPeer::readBodyHandler(const asio::error_code &error,
                         std::size_t bytes_transferred)
{
    if (!error)
    {
        recvMessage();
        startRead();
    }
    else
    {
        CLOG(WARNING, "Overlay") << "readBodyHandler error: " << error;
        // LATER drop Peer
    }
}

void
TCPPeer::recvMessage()
{
    // FIXME: This can do one-less-copy, given a new unmarshal-from-raw-pointers
    // helper in xdrpp.
    xdr::msg_ptr incoming = xdr::message_t::alloc(mIncomingBody.size());
    memcpy(incoming->raw_data(), mIncomingBody.data(), mIncomingBody.size());
    Peer::recvMessage(std::move(incoming));
}

void
TCPPeer::recvHello(StellarMessagePtr msg)
{
    mHelloTimer.cancel();
    Peer::recvHello(msg);
    if (!mApp.getPeerMaster().isPeerAccepted(shared_from_this()))
    { // we can't accept anymore peer connections
        sendPeers();
        drop();
    }
}

void
TCPPeer::drop()
{
    auto self = shared_from_this();
    auto sock = mSocket;
    mApp.getMainIOService().post(
        [self, sock]()
        {
            self->getApp().getPeerMaster().dropPeer(self);
            sock->shutdown(asio::socket_base::shutdown_both);
            sock->close();
        });
}

///////////////////////////////////////////////////////////////////////
// LoopbackPeer
///////////////////////////////////////////////////////////////////////

LoopbackPeer::LoopbackPeer(Application &app, PeerRole role)
    : Peer(app, role), mRemote(nullptr)
{
}

void
LoopbackPeer::sendMessage(xdr::msg_ptr &&msg)
{
    CLOG(TRACE, "Overlay") << "LoopbackPeer queueing message";
    mQueue.emplace_back(std::move(msg));
    // Possibly flush some queued messages if queue's full.
    while (mQueue.size() > mMaxQueueDepth && !mCorked)
    {
        deliverOne();
    }
}

std::string
LoopbackPeer::getIP()
{
    return "<loopback>";
}

void
LoopbackPeer::drop()
{
    auto self = shared_from_this();
    mApp.getMainIOService().post(
        [self]()
        {
            self->getApp().getPeerMaster().dropPeer(self);
        });
    if (mRemote)
    {
        auto remote = mRemote;
        mRemote->getApp().getMainIOService().post(
            [remote]()
            {
                remote->getApp().getPeerMaster().dropPeer(remote);
                remote->mRemote = nullptr;
            });
        mRemote = nullptr;
    }
}

static bool
damageMessage(default_random_engine &gen, xdr::msg_ptr &msg)
{
    size_t bitsFlipped = 0;
    char *d = msg->raw_data();
    char *e = msg->end();
    size_t sz = e - d;
    if (sz > 0)
    {
        auto dist = uniform_int_distribution<size_t>(0, sz - 1);
        auto byteDist = uniform_int_distribution<int>(0, 7);
        size_t nDamage = dist(gen);
        while (nDamage != 0)
        {
            --nDamage;
            auto pos = dist(gen);
            d[pos] ^= 1 << byteDist(gen);
            bitsFlipped++;
        }
    }
    return bitsFlipped != 0;
}

static xdr::msg_ptr &&
duplicateMessage(xdr::msg_ptr const &msg)
{
    xdr::msg_ptr msg2 = xdr::message_t::alloc(msg->size());
    memcpy(msg2->raw_data(), msg->raw_data(), msg->raw_size());
    return std::move(msg2);
}

void
LoopbackPeer::deliverOne()
{
    CLOG(TRACE, "Overlay") << "LoopbackPeer attempting to deliver message";
    if (!mRemote)
    {
        throw std::runtime_error("LoopbackPeer missing target");
    }

    if (!mQueue.empty() && !mCorked)
    {
        xdr::msg_ptr msg = std::move(mQueue.front());
        mQueue.pop_front();

        CLOG(TRACE, "Overlay") << "LoopbackPeer dequeued message";

        // Possibly duplicate the message and requeue it at the front.
        if (mDuplicateProb(mGenerator))
        {
            CLOG(TRACE, "Overlay") << "LoopbackPeer duplicated message";
            mQueue.emplace_front(std::move(duplicateMessage(msg)));
            mStats.messagesDuplicated++;
        }

        // Possibly requeue it at the back and return, reordering.
        if (mReorderProb(mGenerator))
        {
            CLOG(TRACE, "Overlay") << "LoopbackPeer reordered message";
            mQueue.emplace_back(std::move(msg));
            mStats.messagesReordered++;
            return;
        }

        // Possibly flip some bits in the message.
        if (mDamageProb(mGenerator))
        {
            CLOG(TRACE, "Overlay") << "LoopbackPeer damaged message";
            if (damageMessage(mGenerator, msg))
                mStats.messagesDamaged++;
        }

        // Possibly just drop the message on the floor.
        if (mDropProb(mGenerator))
        {
            CLOG(TRACE, "Overlay") << "LoopbackPeer dropped message";
            mStats.messagesDropped++;
            return;
        }

        mStats.bytesDelivered += msg->raw_size();

        // Pass ownership of a serialized XDR message buffer to a recvMesage
        // callback event against the remote Peer, posted on the remote
        // Peer's io_service.
        auto remote = mRemote;
        remote->getApp().getMainIOService().post(std::bind(
            [remote](xdr::msg_ptr const &msg)
            {
                remote->recvMessage(msg);
            },
            std::move(msg)));

        CLOG(TRACE, "Overlay") << "LoopbackPeer posted message to remote";
    }
}

void
LoopbackPeer::deliverAll()
{
    while (!mQueue.empty() && !mCorked)
    {
        deliverOne();
    }
}

void
LoopbackPeer::dropAll()
{
    mQueue.clear();
}

size_t
LoopbackPeer::getBytesQueued() const
{
    size_t t = 0;
    for (auto const &m : mQueue)
    {
        t += m->raw_size();
    }
    return t;
}

size_t
LoopbackPeer::getMessagesQueued() const
{
    return mQueue.size();
}

LoopbackPeer::Stats const &
LoopbackPeer::getStats() const
{
    return mStats;
}

bool
LoopbackPeer::getCorked() const
{
    return mCorked;
}

void
LoopbackPeer::setCorked(bool c)
{
    mCorked = c;
}

int
LoopbackPeer::getMaxQueueDepth() const
{
    return mMaxQueueDepth;
}

void
LoopbackPeer::setMaxQueueDepth(size_t sz)
{
    mMaxQueueDepth = sz;
}

double
LoopbackPeer::getDamageProbability() const
{
    return mDamageProb.p();
}

static void
checkProbRange(double d)
{
    if (d < 0.0 || d > 1.0)
    {
        throw std::runtime_error("probability out of range");
    }
}

void
LoopbackPeer::setDamageProbability(double d)
{
    checkProbRange(d);
    mDamageProb = bernoulli_distribution(d);
}

double
LoopbackPeer::getDropProbability() const
{
    return mDropProb.p();
}

void
LoopbackPeer::setDropProbability(double d)
{
    checkProbRange(d);
    mDamageProb = bernoulli_distribution(d);
}

double
LoopbackPeer::getDuplicateProbability() const
{
    return mDuplicateProb.p();
}

void
LoopbackPeer::setDuplicateProbability(double d)
{
    checkProbRange(d);
    mDamageProb = bernoulli_distribution(d);
}

double
LoopbackPeer::getReorderProbability() const
{
    return mReorderProb.p();
}

void
LoopbackPeer::setReorderProbability(double d)
{
    checkProbRange(d);
    mDamageProb = bernoulli_distribution(d);
}

LoopbackPeerConnection::LoopbackPeerConnection(Application &initiator,
                                               Application &acceptor)
    : mInitiator(make_shared<LoopbackPeer>(initiator, Peer::INITIATOR)),
      mAcceptor(make_shared<LoopbackPeer>(acceptor, Peer::ACCEPTOR))
{
    mInitiator->mRemote = mAcceptor;
    mInitiator->mState = Peer::CONNECTED;

    mAcceptor->mRemote = mInitiator;
    mAcceptor->mState = Peer::CONNECTED;

    initiator.getPeerMaster().addPeer(mInitiator);
    acceptor.getPeerMaster().addPeer(mAcceptor);
}

LoopbackPeerConnection::~LoopbackPeerConnection()
{
    // NB: Dropping the peer from one side will automatically drop the
    // other.
    mInitiator->drop();
}
}
