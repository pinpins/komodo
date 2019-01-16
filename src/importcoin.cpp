/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "crosschain.h"
#include "importcoin.h"
#include "cc/utils.h"
#include "coins.h"
#include "hash.h"
#include "script/cc.h"
#include "primitives/transaction.h"
#include "core_io.h"
#include "script/sign.h"
#include "wallet/wallet.h"

int32_t komodo_nextheight();

CTransaction MakeImportCoinTransaction(const TxProof proof, const CTransaction burnTx, const std::vector<CTxOut> payouts)
{
    std::vector<uint8_t> payload = E_MARSHAL(ss << EVAL_IMPORTCOIN);
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    mtx.vin.push_back(CTxIn(COutPoint(burnTx.GetHash(), 10e8), CScript() << payload));
    mtx.vout = payouts;
    auto importData = E_MARSHAL(ss << proof; ss << burnTx);
    mtx.vout.insert(mtx.vout.begin(), CTxOut(0, CScript() << OP_RETURN << importData));
    return CTransaction(mtx);
}


CTxOut MakeBurnOutput(CAmount value, uint32_t targetCCid, std::string targetSymbol, const std::vector<CTxOut> payouts,std::vector<uint8_t> rawproof)
{
    std::vector<uint8_t> opret;
    opret = E_MARSHAL(ss << VARINT(targetCCid);
                      ss << targetSymbol;
                      ss << SerializeHash(payouts);
                      ss << rawproof);
    return CTxOut(value, CScript() << OP_RETURN << opret);
}


bool UnmarshalImportTx(const CTransaction &importTx, TxProof &proof, CTransaction &burnTx,
        std::vector<CTxOut> &payouts)
{
    std::vector<uint8_t> vData;
    GetOpReturnData(importTx.vout[0].scriptPubKey, vData);
    if (importTx.vout.size() < 1) return false;
    payouts = std::vector<CTxOut>(importTx.vout.begin()+1, importTx.vout.end());
    return importTx.vin.size() == 1 &&
           importTx.vin[0].scriptSig == (CScript() << E_MARSHAL(ss << EVAL_IMPORTCOIN)) &&
           E_UNMARSHAL(vData, ss >> proof; ss >> burnTx);
}


bool UnmarshalBurnTx(const CTransaction &burnTx, std::string &targetSymbol, uint32_t *targetCCid, uint256 &payoutsHash,std::vector<uint8_t>&rawproof)
{
    std::vector<uint8_t> burnOpret; uint32_t ccid = 0;
    if (burnTx.vout.size() == 0) return false;
    GetOpReturnData(burnTx.vout.back().scriptPubKey, burnOpret);
    E_UNMARSHAL(burnOpret, ss >> VARINT(ccid));
    /*if ( ccid != 0xffffffff )
    {
        return E_UNMARSHAL(burnOpret, ss >> VARINT(*targetCCid);
                                    ss >> targetSymbol;
                                    ss >> payoutsHash);
    }
    else*/
    {
        return E_UNMARSHAL(burnOpret, ss >> VARINT(*targetCCid);
                           ss >> targetSymbol;
                           ss >> payoutsHash;
                           ss >> rawproof);
    }
}


/*
 * Required by main
 */
CAmount GetCoinImportValue(const CTransaction &tx)
{
    TxProof proof;
    CTransaction burnTx;
    std::vector<CTxOut> payouts;
    if (UnmarshalImportTx(tx, proof, burnTx, payouts)) {
        return burnTx.vout.size() ? burnTx.vout.back().nValue : 0;
    }
    return 0;
}


/*
 * CoinImport is different enough from normal script execution that it's not worth
 * making all the mods neccesary in the interpreter to do the dispatch correctly.
 */
bool VerifyCoinImport(const CScript& scriptSig, TransactionSignatureChecker& checker, CValidationState &state)
{
    auto pc = scriptSig.begin();
    opcodetype opcode;
    std::vector<uint8_t> evalScript;
    
    auto f = [&] () {
        if (!scriptSig.GetOp(pc, opcode, evalScript))
            return false;
        if (pc != scriptSig.end())
            return false;
        if (evalScript.size() == 0)
            return false;
        if (evalScript.begin()[0] != EVAL_IMPORTCOIN)
            return false;
        // Ok, all looks good so far...
        CC *cond = CCNewEval(evalScript);
        bool out = checker.CheckEvalCondition(cond);
        cc_free(cond);
        return out;
    };

    return f() ? true : state.Invalid(false, 0, "invalid-coin-import");
}


void AddImportTombstone(const CTransaction &importTx, CCoinsViewCache &inputs, int nHeight)
{
    uint256 burnHash = importTx.vin[0].prevout.hash;
    //fprintf(stderr,"add tombstone.(%s)\n",burnHash.GetHex().c_str());
    CCoinsModifier modifier = inputs.ModifyCoins(burnHash);
    modifier->nHeight = nHeight;
    modifier->nVersion = 4;//1;
    modifier->vout.push_back(CTxOut(0, CScript() << OP_0));
}


void RemoveImportTombstone(const CTransaction &importTx, CCoinsViewCache &inputs)
{
    uint256 burnHash = importTx.vin[0].prevout.hash;
    //fprintf(stderr,"remove tombstone.(%s)\n",burnHash.GetHex().c_str());
    inputs.ModifyCoins(burnHash)->Clear();
}


int ExistsImportTombstone(const CTransaction &importTx, const CCoinsViewCache &inputs)
{
    uint256 burnHash = importTx.vin[0].prevout.hash;
    //fprintf(stderr,"check tombstone.(%s) in %s\n",burnHash.GetHex().c_str(),importTx.GetHash().GetHex().c_str());
    return inputs.HaveCoins(burnHash);
}
