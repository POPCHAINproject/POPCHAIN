// Copyright (c) 2017-2018 The Popchain Core Developers

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    static const int32_t CURRENT_VERSION=1;
    int32_t nVersion;
    uint256 hashPrevBlock;
	/*popchain ghost*/
	uint256 hashUncles;//the hash256 of uncles or uncle block header
	uint160 nCoinbase;//the autor address of this block header
    uint64_t nDifficulty;//the difficulty of this block
	/*popchain ghost*/
    uint256 hashMerkleRoot;
    uint256 hashClaimTrie; 							   // for claim operation
    uint32_t nTime;
    uint32_t nBits;
	uint256 nNonce;

    CBlockHeader()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(hashPrevBlock);
		/*popchain ghost*/
		READWRITE(hashUncles);
		READWRITE(nCoinbase);
		READWRITE(nDifficulty);
		/*popchain ghost*/
        READWRITE(hashMerkleRoot);
        READWRITE(hashClaimTrie);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    void SetNull()
    {
        nVersion = CBlockHeader::CURRENT_VERSION;
        hashPrevBlock.SetNull();
		/*popchain ghost*/
		hashUncles.SetNull();
		nCoinbase.SetNull();
        nDifficulty = 0;
		/*popchain ghost*/
        hashMerkleRoot.SetNull();
        hashClaimTrie.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce.SetNull();
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

	std::string ToString() const;
};


// Popchain DevTeam
class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransaction> vtx;

    // memory only
    mutable CTxOut txoutFound; 			// Found  payment
    /*popchain ghost*/
	std::vector <CTxOut> vTxoutUncle; //uncle miner payment
	/*popchain ghost*/
    mutable bool fChecked;

	/*popchain ghost*/
	std::vector<CBlockHeader> vuh;//vector of uncles or uncle block header
	uint256 td;//total difficulty of this block header
	/*popchain ghost*/

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *((CBlockHeader*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CBlockHeader*)this);
        READWRITE(vtx);
		/*popchain ghost*/
		READWRITE(vuh);
		//READWRITE(td);
		/*popchain ghost*/
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
		/*popchain ghost*/
		vuh.clear();
		/*popchain ghost*/
        fChecked = false;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
		/*popchain ghost*/
		block.hashUncles = hashUncles;
		block.nCoinbase = nCoinbase;
		block.nDifficulty = nDifficulty;
		/*popchian ghost*/
        block.hashMerkleRoot = hashMerkleRoot;
		block.hashClaimTrie   = hashClaimTrie;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        return block;
    }

    std::string ToString() const;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    CBlockLocator(const std::vector<uint256>& vHaveIn)
    {
        vHave = vHaveIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

/*popchain ghost*/
uint256 BlockUncleRoot(const CBlock& block);

/*popchain ghost*/


#endif // BITCOIN_PRIMITIVES_BLOCK_H
