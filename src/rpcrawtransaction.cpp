// Copyright (c) 2017-2018 The Popchain Core Developers

#include <sys/time.h>
#include "utilstrencodings.h"
#include "crypto/sha256.h"
#include "base58.h"
#include "chain.h"
#include "coins.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "keystore.h"
#include "main.h"
#include "merkleblock.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpcserver.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "utilmoneystr.h"
#include "instantx.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif
///////////////////////////////////////////////////////////
#include "arith_uint256.h"
#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include "nameclaim.h"
#include <time.h>
//////////////////////////////////////////////////////////
#include <stdint.h>

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <univalue.h>

using namespace std;

typedef vector<unsigned char> valtype;

void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out.push_back(Pair("asm", ScriptToAsmStr(scriptPubKey)));
    if (fIncludeHex)
        out.push_back(Pair("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end())));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        out.push_back(Pair("type", GetTxnOutputType(type)));
        return;
    }

    out.push_back(Pair("reqSigs", nRequired));
    out.push_back(Pair("type", GetTxnOutputType(type)));

    UniValue a(UniValue::VARR);
    BOOST_FOREACH(const CTxDestination& addr, addresses)
        a.push_back(CBitcoinAddress(addr).ToString());
    out.push_back(Pair("addresses", a));
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry)
{
    uint256 txid = tx.GetHash();
    entry.push_back(Pair("txid", txid.GetHex()));
    entry.push_back(Pair("size", (int)::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION)));
    entry.push_back(Pair("version", tx.nVersion));
    entry.push_back(Pair("locktime", (int64_t)tx.nLockTime));
    UniValue vin(UniValue::VARR);
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.push_back(Pair("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
        else {
            in.push_back(Pair("txid", txin.prevout.hash.GetHex()));
            in.push_back(Pair("vout", (int64_t)txin.prevout.n));
            UniValue o(UniValue::VOBJ);
            o.push_back(Pair("asm", ScriptToAsmStr(txin.scriptSig, true)));
            o.push_back(Pair("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
            in.push_back(Pair("scriptSig", o));

            // Add address and value info if spentindex enabled
            CSpentIndexValue spentInfo;
            CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
            if (GetSpentIndex(spentKey, spentInfo)) {
                in.push_back(Pair("value", ValueFromAmount(spentInfo.satoshis)));
                in.push_back(Pair("valueSat", spentInfo.satoshis));
                if (spentInfo.addressType == 1) {
                    in.push_back(Pair("address", CBitcoinAddress(CKeyID(spentInfo.addressHash)).ToString()));
                } else if (spentInfo.addressType == 2)  {
                    in.push_back(Pair("address", CBitcoinAddress(CScriptID(spentInfo.addressHash)).ToString()));
                }
            }

        }
        in.push_back(Pair("sequence", (int64_t)txin.nSequence));
        vin.push_back(in);
    }
    entry.push_back(Pair("vin", vin));
    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];
        UniValue out(UniValue::VOBJ);
        out.push_back(Pair("value", ValueFromAmount(txout.nValue)));
        out.push_back(Pair("valueSat", txout.nValue));
        out.push_back(Pair("n", (int64_t)i));
        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.push_back(Pair("scriptPubKey", o));

        // Add spent information if spentindex is enabled
        CSpentIndexValue spentInfo;
        CSpentIndexKey spentKey(txid, i);
        if (GetSpentIndex(spentKey, spentInfo)) {
            out.push_back(Pair("spentTxId", spentInfo.txid.GetHex()));
            out.push_back(Pair("spentIndex", (int)spentInfo.inputIndex));
            out.push_back(Pair("spentHeight", spentInfo.blockHeight));
        }

        vout.push_back(out);
    }
    entry.push_back(Pair("vout", vout));

    if (!hashBlock.IsNull()) {
        entry.push_back(Pair("blockhash", hashBlock.GetHex()));
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                entry.push_back(Pair("height", pindex->nHeight));
                entry.push_back(Pair("confirmations", 1 + chainActive.Height() - pindex->nHeight));
                entry.push_back(Pair("time", pindex->GetBlockTime()));
                entry.push_back(Pair("blocktime", pindex->GetBlockTime()));
            } else {
                entry.push_back(Pair("height", -1));
                entry.push_back(Pair("confirmations", 0));
            }
        }
    }
}

UniValue getrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"
            "\nNOTE: By default this function only works sometimes. This is when the tx is in the mempool\n"
            "or there is an unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option.\n"
            "\nReturn the raw transaction data.\n"
            "\nIf verbose=0, returns a string that is serialized, hex-encoded data for 'txid'.\n"
            "If verbose is non-zero, returns an Object with information about 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (numeric, optional, default=0) If 0, return a string, other return a json object\n"

            "\nResult (if verbose is not set or set to 0):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose > 0):\n"
            "{\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"popaddress\"        (string) pop address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" 1")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", 1")
        );

    LOCK(cs_main);

    uint256 hash = ParseHashV(params[0], "parameter 1");

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

    string strHex = EncodeHexTx(tx);

    if (!fVerbose)
        return strHex;

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", strHex));
    TxToJSON(tx, hashBlock, result);
    return result;
}

UniValue gettxoutproof(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1 && params.size() != 2))
        throw runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included in manually (by blockhash).\n"
            "\nReturn the raw transaction data.\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"block hash\"  (string, optional) If specified, looks for txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, hex-encoded data for the proof.\n"
        );

    set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = params[0].get_array();
    for (unsigned int idx = 0; idx < txids.size(); idx++) {
        const UniValue& txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid txid ")+txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated txid: ")+txid.get_str());
       setTxids.insert(hash);
       oneTxid = hash;
    }

    LOCK(cs_main);

    CBlockIndex* pblockindex = NULL;

    uint256 hashBlock;
    if (params.size() > 1)
    {
        hashBlock = uint256S(params[1].get_str());
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        pblockindex = mapBlockIndex[hashBlock];
    } else {
        CCoins coins;
        if (pcoinsTip->GetCoins(oneTxid, coins) && coins.nHeight > 0 && coins.nHeight <= chainActive.Height())
            pblockindex = chainActive[coins.nHeight];
    }

    if (pblockindex == NULL)
    {
        CTransaction tx;
        if (!GetTransaction(oneTxid, tx, Params().GetConsensus(), hashBlock, false) || hashBlock.IsNull())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        pblockindex = mapBlockIndex[hashBlock];
    }

    CBlock block;
    if(!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    BOOST_FOREACH(const CTransaction&tx, block.vtx)
        if (setTxids.count(tx.GetHash()))
            ntxFound++;
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "(Not all) transactions not found in specified block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

UniValue verifytxoutproof(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof is invalid\n"
        );

    CDataStream ssMB(ParseHexV(params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    vector<uint256> vMatch;
    if (merkleBlock.txn.ExtractMatches(vMatch) != merkleBlock.header.hashMerkleRoot)
        return res;

    LOCK(cs_main);

    if (!mapBlockIndex.count(merkleBlock.header.GetHash()) || !chainActive.Contains(mapBlockIndex[merkleBlock.header.GetHash()]))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");

    BOOST_FOREACH(const uint256& hash, vMatch)
        res.push_back(hash.GetHex());
    return res;
}

UniValue createrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":amount,\"data\":\"hex\",...} ( locktime )\n"
            "\nCreate a transaction spending the given inputs and creating new outputs.\n"
            "Outputs can be addresses or data.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"transactions\"        (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",    (string, required) The transaction id\n"
            "         \"vout\":n        (numeric, required) The output number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"             (string, required) a json object with outputs\n"
            "    {\n"
            "      \"address\": x.xxx   (numeric or string, required) The key is the pop address, the numeric value (can be string) is the " + CURRENCY_UNIT + " amount\n"
            "      \"data\": \"hex\",     (string, required) The key is \"data\", the value is hex encoded data\n"
            "      ...\n"
            "    }\n"
            "3. locktime                (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
            "\nResult:\n"
            "\"transaction\"            (string) hex string of the transaction\n"

            "\nExamples\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"data\\\":\\\"00010203\\\"}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"data\\\":\\\"00010203\\\"}\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ)(UniValue::VNUM), true);
    if (params[0].isNull() || params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = params[0].get_array();
    UniValue sendTo = params[1].get_obj();

    CMutableTransaction rawTx;

    if (params.size() > 2 && !params[2].isNull()) {
        int64_t nLockTime = params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence = (rawTx.nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());
        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    set<CBitcoinAddress> setAddress;
    vector<string> addrList = sendTo.getKeys();
    BOOST_FOREACH(const string& name_, addrList) {

        if (name_ == "data") {
            std::vector<unsigned char> data = ParseHexV(sendTo[name_].getValStr(),"Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CBitcoinAddress address(name_);
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Pop address: ")+name_);

            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+name_);
            setAddress.insert(address);

            CScript scriptPubKey = GetScriptForDestination(address.Get());
            CAmount nAmount = AmountFromValue(sendTo[name_]);

            CTxOut out(nAmount, scriptPubKey);
            rawTx.vout.push_back(out);
        }
    }

    return EncodeHexTx(rawTx);
}

UniValue decoderawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hex\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"   (string) Pop address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    CTransaction tx;

    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue result(UniValue::VOBJ);
    TxToJSON(tx, uint256(), result);

    return result;
}

UniValue decodescript(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decodescript \"hex\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hex\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) pop address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\",\"address\" (string) script address\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("decodescript", "\"hexstring\"")
            + HelpExampleRpc("decodescript", "\"hexstring\"")
        );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (params[0].get_str().size() > 0){
        vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToJSON(script, r, false);

    r.push_back(Pair("p2sh", CBitcoinAddress(CScriptID(script)).ToString()));
    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("txid", txin.prevout.hash.ToString()));
    entry.push_back(Pair("vout", (uint64_t)txin.prevout.n));
    entry.push_back(Pair("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
    entry.push_back(Pair("sequence", (uint64_t)txin.nSequence));
    entry.push_back(Pair("error", strMessage));
    vErrorsRet.push_back(entry);
}

UniValue signrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase() + "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\"    (string, required for P2SH) redeem script\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privatekeys\"     (string, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"")
            + HelpExampleRpc("signrawtransaction", "\"myhex\"")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VARR)(UniValue::VARR)(UniValue::VSTR), true);

    vector<unsigned char> txData(ParseHexV(params[0], "argument 1"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    vector<CMutableTransaction> txVariants;
    while (!ssData.empty()) {
        try {
            CMutableTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (const std::exception&) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        BOOST_FOREACH(const CTxIn& txin, mergedTx.vin) {
            const uint256& prevHash = txin.prevout.hash;
            CCoins coins;
            view.AccessCoins(prevHash); // this is certainly allowed to fail
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (params.size() > 2 && !params[2].isNull()) {
        fGivenKeys = true;
        UniValue keys = params[2].get_array();
        for (unsigned int idx = 0; idx < keys.size(); idx++) {
            UniValue k = keys[idx];
            CBitcoinSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get_str());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key = vchSecret.GetKey();
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwalletMain)
        EnsureWalletIsUnlocked();
#endif

    // Add previous txouts given in the RPC call:
    if (params.size() > 1 && !params[1].isNull()) {
        UniValue prevTxs = params[1].get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)("scriptPubKey", UniValue::VSTR));

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                CCoinsModifier coins = view.ModifyCoins(txid);
                if (coins->IsAvailable(nOut) && coins->vout[nOut].scriptPubKey != scriptPubKey) {
                    string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coins->vout[nOut].scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                if ((unsigned int)nOut >= coins->vout.size())
                    coins->vout.resize(nOut+1);
                coins->vout[nOut].scriptPubKey = scriptPubKey;
                coins->vout[nOut].nValue = 0; // we don't know the actual output value
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash()) {
                RPCTypeCheckObj(prevOut, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)("scriptPubKey", UniValue::VSTR)("redeemScript",UniValue::VSTR));
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull()) {
                    vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwalletMain) ? tempKeystore : *pwalletMain);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    int nHashType = SIGHASH_ALL;
    if (params.size() > 3 && !params[3].isNull()) {
        static map<string, int> mapSigHashValues =
            boost::assign::map_list_of
            (string("ALL"), int(SIGHASH_ALL))
            (string("ALL|ANYONECANPAY"), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))
            (string("NONE"), int(SIGHASH_NONE))
            (string("NONE|ANYONECANPAY"), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY))
            (string("SINGLE"), int(SIGHASH_SINGLE))
            (string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY))
            ;
        string strHashType = params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const CCoins* coins = view.AccessCoins(txin.prevout.hash);
        if (coins == NULL || !coins->IsAvailable(txin.prevout.n)) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = coins->vout[txin.prevout.n].scriptPubKey;

        txin.scriptSig.clear();
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            SignSignature(keystore, prevPubKey, mergedTx, i, nHashType);

        // ... and merge in other signatures:
        BOOST_FOREACH(const CMutableTransaction& txv, txVariants) {
            txin.scriptSig = CombineSignatures(prevPubKey, mergedTx, i, txin.scriptSig, txv.vin[i].scriptSig);
        }
        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, MutableTransactionSignatureChecker(&mergedTx, i), &serror)) {
            TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", EncodeHexTx(mergedTx)));
    result.push_back(Pair("complete", fComplete));
    if (!vErrors.empty()) {
        result.push_back(Pair("errors", vErrors));
    }

    return result;
}

UniValue sendrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees instantsend )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees  (boolean, optional, default=false) Allow high fees\n"
            "3. instantsend    (boolean, optional, default=false) Use InstantSend to send this transaction\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n"
            + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VBOOL)(UniValue::VBOOL));

    // parse hex string from parameter
    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    uint256 hashTx = tx.GetHash();

    bool fOverrideFees = false;
    if (params.size() > 1)
        fOverrideFees = params[1].get_bool();

    bool fInstantSend = false;
    if (params.size() > 2)
        fInstantSend = params[2].get_bool();

    CCoinsViewCache &view = *pcoinsTip;
    const CCoins* existingCoins = view.AccessCoins(hashTx);
    bool fHaveMempool = mempool.exists(hashTx);
    bool fHaveChain = existingCoins && existingCoins->nHeight < 1000000000;
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CValidationState state;
        bool fMissingInputs;
        if (!AcceptToMemoryPool(mempool, state, tx, false, &fMissingInputs, false, !fOverrideFees)) {
            if (state.IsInvalid()) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            } else {
                if (fMissingInputs) {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                }
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
            }
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }
    if (fInstantSend && !instantsend.ProcessTxLockRequest(tx)) {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Not a valid InstantSend transaction, see debug.log for more info");
    }
    RelayTransaction(tx);

    return hashTx.GetHex();
}

#ifdef ENABLE_WALLET



UniValue atomicswapfirsttx(const UniValue &params, bool fHelp)
{
	if (fHelp || params.size() < 2 || params.size() > 3)
		throw runtime_error(
			"atomicswapfirsttx \"address1\" amount\" address2\n"
			"\nCreate the start atomic swap transaction spending the given inputs.\n"
			"\nArguments:\n"
			"1. \"address1\"  (string,required) The PopChain address to send .\n"
			"2. \"amount\"	  (numeric,required) The amount in " + CURRENCY_UNIT + " to send. eg 0.01\n"
			"3. \"address2\"  (string,optional) The PopChain address to refund  .\n"
			"\nResult:\n"
			"\"lockTime\"        (string) The lock time\n"
			"\"refundAddress\"   (string) The refund address encode by base58\n"
			"\"transactionhash\" (string) The transaction hash\n"
			"\"transaction\"	 (string) hex string of the transaction\n"
			"\"htlcHash\"		 (string) The hash of hash time lock contract encode by base58\n"
			"\"htlc\"			 (string) The hash time lock contract in hex\n"
			"\"rawhash\"		 (string) The raw data of hashlock in hex\n"
			"\"lockhash\"		 (string) The lock hash encode by base58\n"
			"\nExamples:\n"
			+ HelpExampleCli("atomicswapfirsttx", "\"pUwZn7LXgJTpkYdKQQj4L5b2vUJRPTYV4X\" 0.1")
		);
	
	
	LOCK(cs_main);
	
    //check params size is zero or not
    if ((params[0].get_str().size() <= 0)||(params[1].get_str().size() <= 0)){
			throw JSONRPCError(RPC_INVALID_PARAMS, "Error:the parameter size can't be zero");
		}

	// parse the parmater 0 to get receiver address
	CBitcoinAddress recAdr(params[0].get_str());
	
	// check the recevier address is valid popchain address or not
	if (!recAdr.IsValid()){
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid popchain address");
		}
		
	// parse the parmater 1 to get the value to send  
	CAmount nSdValue = AmountFromValue(params[1]);
	
	// check the value is valid or not 
	if (nSdValue <= 0){
			throw JSONRPCError(RPC_TYPE_ERROR, "Invalid value to send");
		}
			
	// get the raw data of lockhash for hash time lock contract
	unsigned char uchR[32];
	memset(uchR, 0, sizeof(uchR));
	RandAddSeedPerfmon();
	GetRandBytes(uchR, sizeof(uchR));
	uint256 rawHash = Hash(uchR,uchR+sizeof(uchR));

	// do hash256 to raw data to get the lock hash fo hash time lock contract
	std::vector<unsigned char> vLockHash;

	//format the vector type raw data to string type,prepare to do RIPMED160 hash calculation 
	std::string strLockHashRip = rawHash.ToString();
	vLockHash.clear();
	vLockHash.resize(20);
	
	// The raw data do the RIPMED160 hash calculation 
	std::vector<unsigned char>vLockHashRip = ParseHex(strLockHashRip);	
	CRIPEMD160().Write(begin_ptr(vLockHashRip),vLockHashRip.size()).Finalize(begin_ptr(vLockHash));
	
	// get lock time for hash time lock contract
	struct timeval tmpTimeval;
	gettimeofday(&tmpTimeval,NULL);
	// lock time equal current time add two day 172800
	int64_t lockTime = tmpTimeval.tv_sec + 172800;
	char tempChar[100] = {0};
	sprintf(tempChar,"%lx",lockTime);
	std::string strLockTime = tempChar;
	
	// get a refund address from paramter or key pool
	CPubKey refPubKey;
	uint160 uRefAdr;	
	if (params.size() == 3){
			CBitcoinAddress tmpRefAdr(params[2].get_str());
			if (!tmpRefAdr.IsValid())
    			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid PopChain address");
			uRefAdr =  tmpRefAdr.GetUint160();
		}
	else{
			if (!pwalletMain->GetKeyFromPool(refPubKey))
				throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT,"Error: Keypool ran out,please call keypoolrefill first");
			uRefAdr =  refPubKey.GetHash160();
		}
	
	uint160 uRecAdr = recAdr.GetUint160();
	
	// construct hash time lock contract script
	CScript htlc =	CScript() << OP_IF << OP_RIPEMD160 << ToByteVector(vLockHash) << OP_EQUALVERIFY << OP_DUP << OP_HASH160 \
	<< ToByteVector(uRecAdr) << OP_ELSE << lockTime << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_DUP << OP_HASH160\
	<< ToByteVector(uRefAdr) << OP_ENDIF << OP_EQUALVERIFY << OP_CHECKSIG;
	
	//get the hash of hash time lock contract
	CScriptID htlcID = CScriptID(htlc);
	CBitcoinAddress htlcHashAdr;
	htlcHashAdr.Set(htlcID ); 
	
	// set the lock script for transaction.
	CScript htlcP2SHPkScript = GetScriptForDestination(CTxDestination(htlcID));
	
	// set the vout of transaction
	 vector<CRecipient> vSend;
	int nChangePosRet = -1;
	CRecipient tmpRecipient = {htlcP2SHPkScript,nSdValue,false};
	vSend.push_back(tmpRecipient);
	
	// Start building a deal
	CReserveKey tmpReskey(pwalletMain);
	CAmount nFeeNeed = 0;
	std::string strError;
	CWalletTx wtxNew;
	if ( !pwalletMain->CreateTransaction(vSend,wtxNew,tmpReskey,nFeeNeed,nChangePosRet,strError))
		{
			if ( nSdValue + nFeeNeed > pwalletMain->GetBalance() )
			{
				strError = strprintf("Error: This transaction requires a transaction fee of at least %s !",FormatMoney(nFeeNeed));
			}
			LogPrintf("%s() : %s\n",__func__,strError);
			throw JSONRPCError(RPC_WALLET_ERROR,strError);
		}
			
		if ( !pwalletMain->CommitTransaction(wtxNew,tmpReskey) )
			throw JSONRPCError(RPC_WALLET_ERROR,"Error: The transaction was rejected! .");
		
	//declare the return data
	UniValue result(UniValue::VOBJ);
		
	CBitcoinAddress refAdr;
	refAdr.Set(CKeyID(uRefAdr));

	// Base58 encoding the lock hash
	std::string strLockHash = EncodeBase58(vLockHash);
	
	result.push_back(Pair("lockTime",strLockTime));
	result.push_back(Pair("refundAddress",refAdr.ToString()));
	result.push_back(Pair("transactionHash",wtxNew.GetHash().GetHex()));
	result.push_back(Pair("transaction",EncodeHexTx(wtxNew)));
	result.push_back(Pair("htlcHash ",htlcHashAdr.ToString()));
	result.push_back(Pair("htlc",HexStr(htlc.begin(),htlc.end())));
	result.push_back(Pair("rawHash",rawHash.ToString()));
	result.push_back(Pair("lockHash",strLockHash));
	return result;
}

UniValue atomicswapsecondtx(const UniValue &params, bool fHelp)
{
	if (fHelp || params.size() < 3 || params.size() > 4)
		throw runtime_error(
			"atomicswapsecondtx \"address\"amount \"lockhash \n"
			"\nCreate reback atomic swap transaction spending the given inputs .\n"
			"\nArguments:\n"
			"1. \"address1\"  (string,required) The PopChain address to send .\n"
			"2. \"amount\"	  (numeric,required) The amount in " + CURRENCY_UNIT + " to send. eg 0.01\n"
			"3. \"lockhash \" (string,required) The lock hash in hex. \n"
			"4. \"address2\"  (string,optional) The PopChain address to refund	.\n"
			"\nResult:\n"
			"\"lockTime\"		 (string) The lock time\n"
			"\"refundAddress\"	 (string) The refund address encode by base58\n"
			"\"transactionhash\" (string) The transaction hash\n"
			"\"transaction\"	 (string) hex string of the transaction\n"
			"\"htlcHash\"		 (string) The hash of hash time lock contract encode by base58\n"
			"\"htlc\"			 (string) The hash time lock contract in hex\n"
			"\nExamples:\n"
			+ HelpExampleCli("atomicswapsecondtx", "\"pUwZn7LXgJTpkYdKQQj4L5b2vUJRPTYV4X\" 0.1\" rcdug3scZ3uVqMox6w3nLm9m8zE")
		);
	
	LOCK(cs_main);
	
	//check params size is zero or not
	if ((params[0].get_str().size() <= 0)||(params[1].get_str().size() <= 0)||(params[2].get_str().size() <= 0)){
			throw JSONRPCError(RPC_INVALID_PARAMS, "Error:the parameter size can't be zero");
		}
	
	// parse the parmater 0 to get receiver address
	CBitcoinAddress recAdr(params[0].get_str());
	
	// check the recevier address is valid popchain address or not
	if (!recAdr.IsValid()){
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid popchain address");
		}
		
	// parse the parmater 1 to get the value to send  
	CAmount nSdValue = AmountFromValue(params[1]);
	
	// check the value is valid or not 
	if (nSdValue <= 0){
			throw JSONRPCError(RPC_TYPE_ERROR, "Invalid value to send");
		}

	//get the lock hash from parmater 3
	std::vector<unsigned char>vLockHash;
	string tmpStr = params[2].get_str();
	DecodeBase58(tmpStr, vLockHash);
			
	// get lock time for hash time lock contract
	struct timeval tmpTimeval;
	gettimeofday(&tmpTimeval,NULL);
	// lock time equal current time add one day 86400
	int64_t lockTime = tmpTimeval.tv_sec + 86400;
	char tempChar[100] = {0};
	sprintf(tempChar,"%lx",lockTime);
	std::string strLockTime = tempChar;
	
	// get a refund address from paramter or key pool
	CPubKey refPubKey;
	uint160 uRefAdr;	
	if (params.size() == 4){
			CBitcoinAddress tmpRefAdr(params[3].get_str());
			if (!tmpRefAdr.IsValid())
				throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid PopChain address");
			uRefAdr =  tmpRefAdr.GetUint160();
		}
	else{
			if (!pwalletMain->GetKeyFromPool(refPubKey))
				throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT,"Error: Keypool ran out,please call keypoolrefill first");
			uRefAdr =  refPubKey.GetHash160();
		}
	
	uint160 uRecAdr = recAdr.GetUint160();
	
	// construct hash time lock contract script
	CScript htlc =	CScript() << OP_IF << OP_RIPEMD160 << ToByteVector(vLockHash) << OP_EQUALVERIFY << OP_DUP << OP_HASH160 \
	<< ToByteVector(uRecAdr) << OP_ELSE << lockTime << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_DUP << OP_HASH160\
	<< ToByteVector(uRefAdr) << OP_ENDIF << OP_EQUALVERIFY << OP_CHECKSIG;
	
	//get the hash of hash time lock contract
	CScriptID htlcID = CScriptID(htlc);
	CBitcoinAddress htlcHashAdr;
	htlcHashAdr.Set(htlcID ); 
	
	// set the lock script for transaction.
	CScript htlcP2SHPkScript = GetScriptForDestination(CTxDestination(htlcID));
	
	// set the vout of transaction
	 vector<CRecipient> vSend;
	int nChangePosRet = -1;
	CRecipient tmpRecipient = {htlcP2SHPkScript,nSdValue,false};
	vSend.push_back(tmpRecipient);
	
	// Start building a deal
	CReserveKey tmpReskey(pwalletMain);
	CAmount nFeeNeed = 0;
	std::string strError;
	CWalletTx wtxNew;
	if ( !pwalletMain->CreateTransaction(vSend,wtxNew,tmpReskey,nFeeNeed,nChangePosRet,strError))
		{
			if ( nSdValue + nFeeNeed > pwalletMain->GetBalance() )
			{
				strError = strprintf("Error: This transaction requires a transaction fee of at least %s !",FormatMoney(nFeeNeed));
			}
			LogPrintf("%s() : %s\n",__func__,strError);
			throw JSONRPCError(RPC_WALLET_ERROR,strError);
		}
			
		if ( !pwalletMain->CommitTransaction(wtxNew,tmpReskey) )
			throw JSONRPCError(RPC_WALLET_ERROR,"Error: The transaction was rejected! .");
		
	//declare the return data
	UniValue result(UniValue::VOBJ);
		
	CBitcoinAddress refAdr;
	refAdr.Set(CKeyID(uRefAdr));
	
	// Base58 encoding the lock hash
	std::string strLockHash = EncodeBase58(vLockHash);
	
	result.push_back(Pair("lockTime",strLockTime));
	result.push_back(Pair("refundAddress",refAdr.ToString()));
	result.push_back(Pair("transactionHash",wtxNew.GetHash().GetHex()));
	result.push_back(Pair("transaction",EncodeHexTx(wtxNew)));
	result.push_back(Pair("htlcHash ",htlcHashAdr.ToString()));
	result.push_back(Pair("htlc",HexStr(htlc.begin(),htlc.end())));
	return result;

	
}

UniValue atomicswapredeemtx(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() !=3)
        throw runtime_error(
            "atomicswapredeemtx \"htlc\" \"transaction\" \"rawhash\" \n"
            "\nCreate atomicswap redeem transaction spending the given inputs .\n"
            "\nArguments:\n"
            "1. \"htlc \"        (string) The hash time lock contract in hex\n"
            "2. \"transaction\"  (string) hex string of the transaction\n"
            "3. \"rawhash \"     (string) The raw data of hashlock in hex\n"
            "nResult:\n"
            "\"txfee\"           (string) The fee of transacton\n"
            "\"transactionhash\" (string) The transaction hash in hex\n"
            "\"transaction\"     (string) The transaction in hex\n"
        );
	
	LOCK(cs_main);
	
	//check params type
	RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VSTR)(UniValue::VSTR));
		
	//check params size is zero or not
	if ((params[0].get_str().size() <= 0)||(params[1].get_str().size() <= 0)||(params[2].get_str().size() <= 0))
		{
			return JSONRPCError(RPC_INVALID_PARAMS, "Error:the parameter size can't be zero");
		}
	
	//get the hash time lock contract from parameter 0
	string strHtlc = params[0].get_str();
	std::vector<unsigned char>vHtlc = ParseHex(strHtlc);
	CScript htlc(vHtlc.begin(),vHtlc.end());
	
	//split the hash time lock contract
	std::string htlcString	= ScriptToAsmStr(htlc);
	std::vector<std::string> htlcVStr;
	boost::split( htlcVStr, htlcString, boost::is_any_of( " " ), boost::token_compress_on );
	
	// check the hash time lock contract is valid contract or not
	if(!htlc.IsAtomicSwapPaymentScript())
		{
			return JSONRPCError(RPC_INVALID_PARAMS, "Error:the parameter is no stander HTLC");
		}	
	
	//get receiver address hash
	std::vector<unsigned char> vRecAdrHash = ParseHex(htlcVStr[6]);
	uint160 recAdrHash(vRecAdrHash);
	
	//decode the input transaction
	CTransaction inputTx;
	if (!DecodeHexTx(inputTx, params[1].get_str()))
	   throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
	
	//get lock hash from hash time lock contract
	std::vector<unsigned char> contractLockHash = ParseHex(htlcVStr[2]);	
	uint160 uContractLockHash(contractLockHash);
	
	//get htlc hash form parameter 2 the raw hash
	std::vector<unsigned char> rawHashVector =ParseHexV(params[2], "rawHash");
	
	//check the htlc hash in parameter and in hash time lock contract
	std::vector<unsigned char> parameterLockHash(20);
	CRIPEMD160().Write(begin_ptr(rawHashVector), rawHashVector.size()).Finalize(begin_ptr(parameterLockHash));
	uint160 uParameterLockHash(parameterLockHash);	
	if ( 0 != strcmp(uContractLockHash.ToString().c_str(),uParameterLockHash.ToString().c_str()) )
		{
			return JSONRPCError(RPC_INVALID_PARAMS, "Error:the lock hash in parameter not match in in contract");		
		}
	
	//declare transaction
	CMutableTransaction txNew;
	CAmount nFeePay = 3100;
	
	//the redeem amount is input amount - tx fee
	CAmount preOutAmount = 0;
	COutPoint preOutPoint;
	uint256 preTxid = inputTx.GetHash();
	CTxOut preTxOut;
	uint32_t preOutN =0;	
	std::vector<valtype> vSolutions;
	txnouttype addressType = TX_NONSTANDARD;
	uint160 addrhash;
	
	//get the previous tx ouput
	BOOST_FOREACH(const CTxOut& txout, inputTx.vout) 
	{
		const CScript scriptPubkey = StripClaimScriptPrefix(txout.scriptPubKey);
		if (Solver(scriptPubkey, addressType, vSolutions))
		{
			if(addressType== TX_SCRIPTHASH )
			{
				addrhash=uint160(vSolutions[0]);
				preOutAmount =	txout.nValue;
				CTxIn tmptxin = CTxIn(preTxid,preOutN,CScript());
				tmptxin.prevPubKey = txout.scriptPubKey;
				txNew.vin.push_back(tmptxin);
				break;	
			}
		}
		preOutN++;
	}
	
	//check the previous tx output type
	if(addressType !=TX_SCRIPTHASH)
		{
			return JSONRPCError(RPC_INVALID_PARAMS, "Error:the transaction have none P2SH type tx out");	
		}
	
	//check the hash time lock contract is match input transaction or not 
	if ( 0 != strcmp(addrhash.ToString().c_str(),Hash160(vHtlc).ToString().c_str()) )
	{
		return JSONRPCError(RPC_INVALID_PARAMS, "Error:the HTLC in parameter can't match  transaction");
	}
		
	//get the pubkey and key of recevier address
	const CKeyStore& keystore = *pwalletMain;
	CKeyID keyID(recAdrHash);
	CPubKey pubKey;
	CKey key;	
	if(!keystore.GetPubKey(keyID,pubKey))
		{
			return JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error:Can't find the pubkey of receiver address");			
		}
	if(!keystore.GetKey(keyID,key))
		{
			return JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error:Can't find the key of receiver address");	
		}
	
	//declare the reserverkey for commit transaction
	CReserveKey reservekey(pwalletMain);

	//build the output use the hash time lock contract receiver address
	CBitcoinAddress outPutAddress(CTxDestination(pubKey.GetID()));
	if (!outPutAddress.IsValid())
		return JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error:Invalid PopChain address");
	
	// Start building the lock script for the p2pkh type.
	CScript recP2PkHScript = GetScriptForDestination(CTxDestination(pubKey.GetID()));
	CAmount nAmount = preOutAmount- nFeePay;
	CTxOut outNew(nAmount,recP2PkHScript);
	txNew.vout.push_back(outNew);
	
	txNew.nLockTime = chainActive.Height();
	txNew.nVersion = 1;
	
	// Sign the redeem transaction
	CTransaction txNewConst(txNew);
	std::vector<unsigned char> vchSig;
	CScript scriptSigRs;
	uint256 hash = SignatureHash(htlc, txNew, 0, SIGHASH_ALL);
	bool signSuccess = key.Sign(hash, vchSig);	
	bool verifySuccess = pubKey.Verify(hash,vchSig);
	vchSig.push_back((unsigned char)SIGHASH_ALL);
	
	if(signSuccess)
		{
		CScript script1 =CScript() <<ToByteVector(vchSig);
		CScript script2 =CScript() << ToByteVector(pubKey);
		CScript script3 =CScript() <<ToByteVector(rawHashVector);
		CScript script4 =CScript() << OP_TRUE <<ToByteVector(vHtlc);
		scriptSigRs= script1 + script2 + script3 + script4;
		txNew.vin[0].scriptSig = scriptSigRs;		
		}
	else
		{
			return JSONRPCError(RPC_INTERNAL_ERROR, "ERROR:sign transaction error");
		}
	
	if(!verifySuccess)
		{
			return JSONRPCError(RPC_INTERNAL_ERROR, "ERROR:verify the sign of transaction error");
		}
	
	//serialize and get the size of transaction
	unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
	
	//check is match limit size or not 
	if (nBytes >= MAX_STANDARD_TX_SIZE)
	{
		return JSONRPCError(RPC_INTERNAL_ERROR, "ERROR:transaction too large");
	}
	
	//check transaction is Dust
	if (txNew.vout[0].IsDust(::minRelayTxFee))
	{
		return JSONRPCError(RPC_INTERNAL_ERROR, "ERROR:transaction is dust transaction");
	}
	
	//commit the redeem transaction 
	 CWalletTx wtxNew;
	 wtxNew.fTimeReceivedIsTxTime = true;
	 wtxNew.BindWallet(pwalletMain);
	 wtxNew.fFromMe = true;
	*static_cast<CTransaction*>(&wtxNew) = CTransaction(txNew); 
	if (!pwalletMain->CommitTransaction(wtxNew, reservekey,NetMsgType::TX))
			return JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
	
	double fFeePay = (double)nFeePay;
	string strRedeemFee = strprintf("tx fee: %.8f Pch(%.8f Pch/kB)\n", (fFeePay / COIN), ((fFeePay / COIN)/nBytes));
	
	//declare return data
	UniValue result(UniValue::VOBJ);
	
	result.push_back(Pair("txfee",strRedeemFee));
	result.push_back(Pair("transactionhash",CTransaction(txNew).GetHash().GetHex()));
	result.push_back(Pair("transaction",EncodeHexTx(CTransaction(txNew))));
	return result;
	
}

UniValue atomicswaprefundtx(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() !=2)
        throw runtime_error(
		"atomicswaprefundtx \"htlc \"transaction \n"
		"\nCreate atomic swap refund transaction spending the given inputs .\n"
		"\nArguments:\n"
		"1. \"htlc \"         (string,required) (string) The hash time lock contract in hex\n"
		"2. \"transaction \"  (string,required) The transaction in hex\n"
		"nResult:\n"
		"\"txfee\"			 (string) The fee of transacton\n"
		"\"transactionhash\" (string) The transaction hash in hex\n"
		"\"transaction\"	 (string) The transaction in hex\n"
        );
	
	LOCK(cs_main);
	RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VSTR));
			
	//check params size is zero or not 
	if (params[0].get_str().size() <= 0||(params[1].get_str().size() <= 0))
		{
			return false;
		}

	//get the hash time lock contract from parameter 0
	string strHtlc = params[0].get_str();
	std::vector<unsigned char>vHtlc = ParseHex(strHtlc);
	CScript htlc(vHtlc.begin(),vHtlc.end());

	//split the hash time lock contract
	std::string htlcString	= ScriptToAsmStr(htlc);
	std::vector<std::string> htlcVStr;
	boost::split( htlcVStr, htlcString, boost::is_any_of( " " ), boost::token_compress_on );
	
	// check the hash time lock contract is valid contract or not
	if(!htlc.IsAtomicSwapPaymentScript())
		{
			return JSONRPCError(RPC_INVALID_PARAMS, "Error:the parameter is no stander HTLC");
		}	
		
	//get lock time 
	int64_t lockTime = atoi64(htlcVStr[8]);
			
	//get refund address hash
	std::vector<unsigned char> vRefAdrHash = ParseHex(htlcVStr[13]);
	uint160 refAdrHash(vRefAdrHash);
			
	//decode the input transaction
	CTransaction inputTx;
	if (!DecodeHexTx(inputTx, params[1].get_str()))
		return JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
					
	//declare refund transaction
	CMutableTransaction txNew;
	CAmount nFeePay = 3100;
	
	//get the refund amount
	CAmount preOutAmount = 0;
	COutPoint preOutPoint;
	uint256 preTxid = inputTx.GetHash();
	CTxOut preTxOut;
	uint32_t preOutN =0;	
	std::vector<valtype> vSolutions;
	txnouttype addressType = TX_NONSTANDARD;
	uint160 addrhash;
		
	BOOST_FOREACH(const CTxOut& txout, inputTx.vout) 
	{
		const CScript scriptPubkey = StripClaimScriptPrefix(txout.scriptPubKey);
		if (Solver(scriptPubkey, addressType, vSolutions))
		{
			if(addressType== TX_SCRIPTHASH )
			{
				addrhash=uint160(vSolutions[0]);
				preOutAmount =	txout.nValue;
				CTxIn tmptxin = CTxIn(preTxid,preOutN,CScript(),(std::numeric_limits<uint32_t>::max()-1));
				tmptxin.prevPubKey = txout.scriptPubKey;
				txNew.vin.push_back(tmptxin);
				break;					
			}
		}
		preOutN++;
	}

	if(addressType !=TX_SCRIPTHASH)
		{
			throw JSONRPCError(RPC_INVALID_PARAMS, "Error:the transaction have none P2SH type tx out");	
		}	

	//check the hash time lock contract is match transaction or not 
	if ( 0 != strcmp(addrhash.ToString().c_str(),Hash160(vHtlc).ToString().c_str()) )
	{
		return JSONRPCError(RPC_INVALID_PARAMS, "Error:the contract in parameter can't match transaction in parameter");
	}	

	//get the pubkey and key of refund address
	const CKeyStore& keystore = *pwalletMain;
	CKeyID keyID(refAdrHash);
	CPubKey pubKey;
	CKey key;	
	if(!keystore.GetPubKey(keyID,pubKey))
		{
			return JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error:Can't find the pubkey of refund address");			
		}
	if(!keystore.GetKey(keyID,key))
		{
			return JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error:Can't find the key of refund address");	
		}

	//get the out pubkey type p2pkh
	CReserveKey reservekey(pwalletMain);
	CBitcoinAddress outPutAddress(CTxDestination(pubKey.GetID()));
	if (!outPutAddress.IsValid())
		return JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid PopChain address");
	
	
	// Start building the lock script for the p2pkh type.
	CScript refP2PkHScript = GetScriptForDestination(CTxDestination(pubKey.GetID()));
	CAmount nAmount = preOutAmount- nFeePay;
	CTxOut outNew(nAmount,refP2PkHScript);
	txNew.vout.push_back(outNew);
	
	txNew.nLockTime = lockTime;
	txNew.nVersion = 1;
	
	// Sign the refund transaction
	CTransaction txNewConst(txNew);
	std::vector<unsigned char> vchSig;
	CScript scriptSigRs;
	uint256 hash = SignatureHash(htlc, txNew, 0, SIGHASH_ALL);
	bool signSuccess = key.Sign(hash, vchSig);	
	bool verifySuccess = pubKey.Verify(hash,vchSig);
	vchSig.push_back((unsigned char)SIGHASH_ALL);	
	if(!verifySuccess)
		{
			return JSONRPCError(RPC_INTERNAL_ERROR, "ERROR:verify the sign of transaction error");
		}
		
	if(signSuccess)
		{
			CScript script1 =CScript() <<ToByteVector(vchSig);
			CScript script2 =CScript() << ToByteVector(pubKey);
			CScript script4 =CScript() << OP_FALSE <<ToByteVector(vHtlc);
			scriptSigRs= script1 + script2 + script4;
			txNew.vin[0].scriptSig = scriptSigRs;				
		}
	else
		{
			return JSONRPCError(RPC_INTERNAL_ERROR, "ERROR:sign transaction error");
		}
				
	//add the sign of transaction
	unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
	
	//check the refund transaction is match limit size or not 
	if (nBytes >= MAX_STANDARD_TX_SIZE)
		{
			return JSONRPCError(RPC_INTERNAL_ERROR, "ERROR:transaction too large");
		}
	
	//check the refund transaction is Dust or not 
	if (txNew.vout[0].IsDust(::minRelayTxFee))
		{
			return JSONRPCError(RPC_INTERNAL_ERROR, "ERROR:transaction is dust transaction");
		}
	
	
	CWalletTx wtxNew;
	wtxNew.fTimeReceivedIsTxTime = true;
	wtxNew.BindWallet(pwalletMain);
	wtxNew.fFromMe = true;
	*static_cast<CTransaction*>(&wtxNew) = CTransaction(txNew);			
	if (!pwalletMain->CommitTransaction(wtxNew, reservekey,NetMsgType::TX))
			return JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
		
	double fFeePay = (double)nFeePay;
	string strRefundFee = strprintf("tx fee: %.8f Pch(%.8f Pch/kB)\n", (fFeePay / COIN), ((fFeePay / COIN)/nBytes));

	//the return data
	UniValue result(UniValue::VOBJ);
	result.push_back(Pair("txfee",strRefundFee));
	result.push_back(Pair("transactionhash",CTransaction(txNew).GetHash().GetHex()));
	result.push_back(Pair("transaction",EncodeHexTx(CTransaction(txNew))));			
	return result;

}



UniValue atomicswapgetrawhash(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() !=1)
        throw runtime_error(
		"atomicswapgetsecret \"transaction \n"
		"\nget raw data of lock hash from atomic redeem transaction spending the given inputs .\n"
		"\nArguments:\n"
		"1. \transaction \"  (string,required) The transaction in hex\n"
		"nResult:\n"
		"\"rawhash\"			 (string) The transaction in hex\n"
        );

	LOCK(cs_main);
	RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));
	
    //check params size is zero or not 
    if (params[0].get_str().size() <= 0)
		{
			return JSONRPCError(RPC_INVALID_PARAMS, "Error:the parameter size can't be zero");
		}
	
	//decode the input transaction
	CTransaction inputTx;
	if (!DecodeHexTx(inputTx, params[0].get_str()))
       return JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
	CScript InputScriptSig = inputTx.vin[0].scriptSig;
	
	//split the scriptSig
	std::string strInputScriptSig  = ScriptToAsmStr(InputScriptSig);
	std::vector<std::string> vStrInpScrSig;
    boost::split( vStrInpScrSig, strInputScriptSig, boost::is_any_of( " " ), boost::token_compress_on );

	//get the hash time lock contract
	std::vector<unsigned char> vHtlc = ParseHex(vStrInpScrSig[4]);
	CScript htlc(vHtlc.begin(),vHtlc.end());
	
	//check the input hash time lock contract is standard HTLC or not 
	if(!htlc.IsAtomicSwapPaymentScript())
		{
			return JSONRPCError(RPC_INVALID_PARAMS, "Error:the parameter is no stander contract");
		}
	
	//split the hash time lock contract
	std::string strHtlc  = ScriptToAsmStr(htlc);
	std::vector<std::string> vStrHtlc;
    boost::split(vStrHtlc, strHtlc, boost::is_any_of( " " ), boost::token_compress_on );	
	
	//get secret hash from contract
	std::vector<unsigned char> htlcRawHash = ParseHex(vStrHtlc[2]);	
	uint160 uHtlcRawHash(htlcRawHash);

    //get secret form script sig
    std::string strRawHash =vStrInpScrSig[2];
	std::vector<unsigned char> vScrSigRawHash =ParseHex(vStrInpScrSig[2]);

	//check the secret in parameter and in contract
	std::vector<unsigned char> txRawHash(20);
	CRIPEMD160().Write(begin_ptr(vScrSigRawHash), vScrSigRawHash.size()).Finalize(begin_ptr(txRawHash));
	uint160 uTxRawHash(txRawHash);	
	if ( 0 != strcmp(uHtlcRawHash.ToString().c_str(),uTxRawHash.ToString().c_str()) )
		{
			return JSONRPCError(RPC_INVALID_PARAMS, "Error:the rawhash in parameter not match in in HTLC");		
		}

	//declare the return data	
    UniValue result(UniValue::VOBJ);

	//return the raw data of lock hash
	result.push_back(Pair("rawhash",strRawHash));
    return result;

}

char * timeReachCalc(int t,char *buf)
{
	int h = t / 3600;
	int m_t = t - 3600 * h;
	int m = m_t / 60;
	int s = m_t - m * 60;
	sprintf(buf,"%dh %dm %ds",h,m,s);
	return buf;
}


UniValue atomicswapchecktx(const UniValue &params, bool fHelp)
{
    if (fHelp || params.size() !=2)
       throw runtime_error(
        "atomicswapchecktx \"htlc\" transaction\n"
        "\nCheck atomic swap transaction and atomic swap hash time lock contract spending the given inputs .\n"
        "\nArguments:\n"
        "1. \"htlc \"         (string,required) (string) The hash time lock contract in hex\n"
		"2. \"transaction \"  (string,required) The contract raw transaction in hex\n"
        "\nResult:\n"
        "\"amount\"          (amount) The transaction for amount \n"
        "\"address1\"        (string) The address of receiver by base58\n"
        "\"address2\"        (string) The address of refund by base58\n"
        "\"lockhash\"        (string) The lock hash in hex\n"
        "\"locktime\"        (string) The lock time \n"
    	);



	LOCK(cs_main);
	RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VSTR));
	
	string strHtlc = params[0].get_str();
	std::vector<unsigned char>vHtlc = ParseHex(strHtlc);
	CScript htlc(vHtlc.begin(),vHtlc.end());
	CScriptID htlcP2SH = CScriptID(htlc);
	CBitcoinAddress htlcAdr;
	
	CTransaction tx;
	if (!DecodeHexTx(tx, params[1].get_str()))
		throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
	
	std::vector<valtype> vSolutions;
	txnouttype addressType;
	uint160 addrhash;
	CAmount value;
	BOOST_FOREACH(const CTxOut& txout, tx.vout) 
	{
		const CScript scriptPubkey = StripClaimScriptPrefix(txout.scriptPubKey);
		if (Solver(scriptPubkey, addressType, vSolutions))
		{
			if(addressType== TX_SCRIPTHASH )
			{
				addrhash=uint160(vSolutions[0]);
				value = txout.nValue;
				break;
			}
			else if(addressType==TX_PUBKEYHASH )
			{
				addrhash=uint160(vSolutions[0]);
				continue;
			}
			else if(addressType== TX_PUBKEY)
			{
				addrhash= Hash160(vSolutions[0]);
				continue;
			}
		}
	}
	
	std::vector<std::string> vHtlcStrV;

	if ( 0 == strcmp(htlcP2SH.ToString().c_str(),addrhash.ToString().c_str()) )
	{
		htlcAdr.Set(htlcP2SH);
		if (!htlc.IsAtomicSwapPaymentScript())
		{
			LogPrintf("HTLC is not an standard contract");
		}
		//split the contract
		std::string htlcString	= ScriptToAsmStr(htlc);
		boost::split( vHtlcStrV, htlcString, boost::is_any_of( " " ), boost::token_compress_on );
	}
	else
	{
		throw JSONRPCError(RPC_INVALID_PARAMS, "TX decode failed");
	}
	
	CBitcoinAddress recAdr;
	CBitcoinAddress refAdr;
	std::vector<unsigned char> vRecAdr = ParseHex(vHtlcStrV[6]);
	std::vector<unsigned char> uRefAdr = ParseHex(vHtlcStrV[13]);
	uint160 uRecKeyId(vRecAdr);
	uint160 uRefKeyId(uRefAdr);
	recAdr.Set((CKeyID&)uRecKeyId);
	refAdr.Set((CKeyID&)uRefKeyId);
	std::string strLockHash = EncodeBase58(ParseHex(vHtlcStrV[2]));
	
	int64_t iLocktime = atoi64(vHtlcStrV[8]);
	
	struct timeval tmpTime;
	gettimeofday(&tmpTime,NULL);
	int64_t currentTime = tmpTime.tv_sec;
	int64_t remainTime = iLocktime - currentTime  ;
	string strTime;
	char tmpBuf[100] = {0};
	timeReachCalc(remainTime,tmpBuf);
	strTime = tmpBuf; 
	
	UniValue result(UniValue::VOBJ);
	result.push_back(Pair("amount",ValueFromAmount(value)));
	result.push_back(Pair("address1",recAdr.ToString()));
	result.push_back(Pair("address2",refAdr.ToString()));
	result.push_back(Pair("lockhash",strLockHash));
	result.push_back(Pair("Locktime:",asctime(localtime(&iLocktime))));
	
	return result;



}



#endif


