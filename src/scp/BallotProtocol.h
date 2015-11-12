#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <memory>
#include <functional>
#include <string>
#include <set>
#include <utility>
#include "scp/SCP.h"
#include "lib/json/json-forwards.h"

namespace stellar
{
class Node;
class Slot;

// used to filter statements
typedef std::function<bool(SCPStatement const& st)> StatementPredicate;

/**
 * The Slot object is in charge of maintaining the state of the SCP protocol
 * for a given slot index.
 */
class BallotProtocol
{
    Slot& mSlot;

    bool mHeardFromQuorum;

    // state tracking members
    enum SCPPhase
    {
        SCP_PHASE_PREPARE,
        SCP_PHASE_CONFIRM,
        SCP_PHASE_EXTERNALIZE,
        SCP_PHASE_NUM
    };
    // human readable names matching SCPPhase
    static const char* phaseNames[];

    std::unique_ptr<SCPBallot> mCurrentBallot;      // b
    std::unique_ptr<SCPBallot> mPrepared;           // p
    std::unique_ptr<SCPBallot> mPreparedPrime;      // p'
    std::unique_ptr<SCPBallot> mHighBallot;         // h
    std::unique_ptr<SCPBallot> mCommit;             // c
    std::map<NodeID, SCPEnvelope> mLatestEnvelopes; // M
    SCPPhase mPhase;                                // Phi

    int mCurrentMessageLevel; // number of messages triggered in one run

    std::unique_ptr<SCPEnvelope>
        mLastEnvelope; // last envelope emitted by this node

  public:
    BallotProtocol(Slot& slot);

    // Process a newly received envelope for this slot and update the state of
    // the slot accordingly.
    // self: set to true when node feeds its own statements in order to
    // trigger more potential state changes
    SCP::EnvelopeState processEnvelope(SCPEnvelope const& envelope, bool self);

    void ballotProtocolTimerExpired();
    // abandon's current ballot, move to a new ballot
    // at counter `n` (or, if n == 0, increment current counter)
    bool abandonBallot(uint32 n);

    // bumps the ballot based on the local state and the value passed in:
    // in prepare phase, attempts to take value
    // otherwise, no-ops
    // force: when true, always bumps the value, otherwise only bumps
    // the state if no value was prepared
    bool bumpState(Value const& value, bool force);
    // flavor that takes the actual desired counter value
    bool bumpState(Value const& value, uint32 n);

    // ** status methods

    // returns information about the local state in JSON format
    // including historical statements if available
    void dumpInfo(Json::Value& ret);

    // returns information about the quorum for a given node
    void dumpQuorumInfo(Json::Value& ret, NodeID const& id, bool summary);

    // returns the hash of the QuorumSet that should be downloaded
    // with the statement.
    // note: the companion hash for an EXTERNALIZE statement does
    // not match the hash of the QSet, but the hash of commitQuorumSetHash
    static Hash getCompanionQuorumSetHashFromStatement(SCPStatement const& st);

    // helper function to retrieve b for PREPARE, P for CONFIRM or
    // c for EXTERNALIZE messages
    static SCPBallot getWorkingBallot(SCPStatement const& st);

    SCPEnvelope*
    getLastMessageSend() const
    {
        return mLastEnvelope.get();
    }

    void setStateFromEnvelope(SCPEnvelope const& e);

    std::vector<SCPEnvelope> getCurrentState() const;

  private:
    // attempts to make progress using the latest statement as a hint
    // calls into the various attempt* methods, emits message
    // to make progress
    void advanceSlot(SCPStatement const& hint);

    // send latest envelope if needed
    void sendLatestEnvelope();

    // `attempt*` methods are called by `advanceSlot` internally call the
    //  the `set*` methods.
    //   * check if the specified state for the current slot has been
    //     reached or not.
    //   * idempotent
    //  input: latest statement received (used as a hint to reduce the
    //  space to explore)
    //  output: returns true if the state was updated

    // `set*` methods progress the slot to the specified state
    //  input: state specific
    //  output: returns true if the state was updated.

    // step 1 and 5 from the SCP paper
    bool attemptPreparedAccept(SCPStatement const& hint);
    // prepared: ballot that should be prepared
    bool setPreparedAccept(SCPBallot const& prepared);

    // step 2+3+8 from the SCP paper
    // ballot is the candidate to record as 'confirmed prepared'
    bool attemptPreparedConfirmed(SCPStatement const& hint);
    // newC, newH : low/high bounds prepared confirmed
    bool setPreparedConfirmed(SCPBallot const& newC, SCPBallot const& newH);

    // step (4 and 6)+8 from the SCP paper
    bool attemptAcceptCommit(SCPStatement const& hint);
    // new values for c and h
    bool setAcceptCommit(SCPBallot const& c, SCPBallot const& h);

    // step 7+8 from the SCP paper
    bool attemptConfirmCommit(SCPStatement const& hint);
    bool setConfirmCommit(SCPBallot const& acceptCommitLow,
                          SCPBallot const& acceptCommitHigh);

    // step 9 from the SCP paper
    bool attemptBump();

    // computes a list of candidate values that may have been prepared
    std::set<SCPBallot> getPrepareCandidates(SCPStatement const& hint);

    // helper to perform step (8) from the paper
    void updateCurrentIfNeeded();

    // An interval is [low,high] represented as a pair
    using Interval = std::pair<uint32, uint32>;

    // helper function to find a contiguous range 'candidate' that satisfies the
    // predicate.
    // updates 'candidate' (or leave it unchanged)
    static void findExtendedInterval(Interval& candidate,
                                     std::set<uint32> const& boundaries,
                                     std::function<bool(Interval const&)> pred);

    // constructs the set of counters representing the
    // commit ballots compatible with the ballot
    std::set<uint32> getCommitBoundariesFromStatements(SCPBallot const& ballot);

    // ** helper predicates that evaluate if a statement satisfies
    // a certain property

    // is ballot prepared by st
    static bool hasPreparedBallot(SCPBallot const& ballot,
                                  SCPStatement const& st);

    // returns true if the statement commits the ballot in the range 'check'
    static bool commitPredicate(SCPBallot const& ballot, Interval const& check,
                                SCPStatement const& st);

    // attempts to update p to ballot (updating p' if needed)
    bool setPrepared(SCPBallot const& ballot);

    // ** Helper methods to compare two ballots

    // ballot comparison (ordering)
    static int compareBallots(std::unique_ptr<SCPBallot> const& b1,
                              std::unique_ptr<SCPBallot> const& b2);
    static int compareBallots(SCPBallot const& b1, SCPBallot const& b2);

    // b1 ~ b2
    static bool areBallotsCompatible(SCPBallot const& b1, SCPBallot const& b2);

    // b1 <= b2 && b1 !~ b2
    static bool areBallotsLessAndIncompatible(SCPBallot const& b1,
                                              SCPBallot const& b2);
    // b1 <= b2 && b1 ~ b2
    static bool areBallotsLessAndCompatible(SCPBallot const& b1,
                                            SCPBallot const& b2);

    // ** statement helper functions

    // returns true if the statement is newer than the one we know about
    // for a given node.
    bool isNewerStatement(NodeID const& nodeID, SCPStatement const& st);

    // returns true if st is newer than oldst
    static bool isNewerStatement(SCPStatement const& oldst,
                                 SCPStatement const& st);

    // basic sanity check on statement
    bool isStatementSane(SCPStatement const& st, bool self);

    // records the statement in the state machine
    void recordEnvelope(SCPEnvelope const& env);

    // ** State related methods

    // helper function that updates the current ballot
    // this is the lowest level method to update the current ballot and as
    // such doesn't do any validation
    void bumpToBallot(SCPBallot const& ballot);

    // switch the local node to the given ballot's value
    // with the assumption that the ballot is more recent than the one
    // we have.
    bool updateCurrentValue(SCPBallot const& ballot);

    // emits a statement reflecting the nodes' current state
    // and attempts to make progress
    void emitCurrentStateStatement();

    // verifies that the internal state is consistent
    void checkInvariants();

    // create a statement of the given type using the local state
    SCPStatement createStatement(SCPStatementType const& type);

    // returns a string representing the slot's state
    // used for log lines
    std::string getLocalState() const;

    std::shared_ptr<LocalNode> getLocalNode();

    bool federatedAccept(StatementPredicate voted, StatementPredicate accepted);
    bool federatedRatify(StatementPredicate voted);

    void startBallotProtocolTimer();
};
}
