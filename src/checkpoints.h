// Copyright (c) 2017-2018 The Popchain Core Developers

#ifndef BITCOIN_CHECKPOINTS_H
#define BITCOIN_CHECKPOINTS_H

#include "uint256.h"
#include "chainparams.h"

#include <map>

class CBlockIndex;
struct CCheckpointData;

/**
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
namespace Checkpoints
{

//! Return conservative estimate of total number of blocks, 0 if unknown
int GetTotalBlocksEstimate(const CCheckpointData& data);

//! Returns last CBlockIndex* in mapBlockIndex that is a checkpoint
CBlockIndex* GetLastCheckpoint(const CCheckpointData& data);
//PairCheckpoints ForceGetLastCheckpoint(const CCheckpointData& data);
uint256 GetHeightCheckpoint(int nHeight ,const CCheckpointData& data);
double GuessVerificationProgress(const CCheckpointData& data, CBlockIndex* pindex, bool fSigchecks = true);

} //namespace Checkpoints

#endif // BITCOIN_CHECKPOINTS_H
