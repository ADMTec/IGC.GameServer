#include "stdafx.h"
#include "DSProtocol.h"
#include "protocol.h"
#include "user.h"
#include "GameMain.h"
#include "MasterSkillSystem.h"
#include "TLog.h"
#include "ObjCalCharacter.h"
#include "winutil.h"
#include "VipSys.h"
#include "configread.h"

CMasterLevelSystem::CMasterLevelSystem() {
    this->gMasterExperience = NULL;
}

CMasterLevelSystem::~CMasterLevelSystem() {
    delete[] this->gMasterExperience;
}

bool CMasterLevelSystem::MasterLevelUp(LPOBJ lpObj, UINT64 addexp, int iMonsterType, const char *szEventType) {
    if (lpObj->Type != OBJ_USER) {
        g_Log.AddC(TColor::Red, "[ERROR] lpObj->Type != OBJ_USER (%s)(%d)", __FILE__, __LINE__);
        return false;
    }

    g_Log.Add("[%s] Master Experience : Map[%s]-(%d,%d) [%s][%s](%d)(%d) %I64d + %I64d MonsterClass : %d",
              szEventType, Lang.GetMap(0, lpObj->MapNumber), lpObj->X, lpObj->Y, lpObj->AccountID, lpObj->Name,
              lpObj->Level, lpObj->m_PlayerData->MasterLevel, lpObj->m_PlayerData->MasterExperience - addexp, addexp,
              iMonsterType);

    if (lpObj->m_PlayerData->ChangeUP != 2) {
        g_Log.AddC(TColor::Red, "[ERROR] lpObj->m_PlayerData->ChangeUp != 2 (%s)(%d)", __FILE__, __LINE__);
        return false;
    }

    if (lpObj->m_PlayerData->MasterLevel >= g_ConfigRead.data.common.MLUserMaxLevel) {
        lpObj->m_PlayerData->MasterExperience = this->gMasterExperience[lpObj->m_PlayerData->MasterLevel];
        GSProtocol.GCServerMsgStringSend(Lang.GetText(0, 45), lpObj->m_Index, 1);
        return false;
    }

    gObjSetExpPetItem(lpObj->m_Index, addexp);

    if (lpObj->m_PlayerData->MasterExperience < lpObj->m_PlayerData->MasterNextExp) {
        return true;
    }

    lpObj->m_PlayerData->MasterLevel++;

    if (g_ConfigRead.data.reset.iBlockMLPointAfterResets == -1 ||
        lpObj->m_PlayerData->m_iResets < g_ConfigRead.data.reset.iBlockMLPointAfterResets) {
        lpObj->m_PlayerData->MasterPoint += g_ConfigRead.data.common.MLPointPerLevel;
    }

    lpObj->m_PlayerData->MasterExperience = lpObj->m_PlayerData->MasterNextExp;

    gObjCalCharacter.CalcCharacter(lpObj->m_Index);

    lpObj->MaxLife += DCInfo.DefClass[lpObj->Class].LevelLife;
    lpObj->MaxMana += DCInfo.DefClass[lpObj->Class].LevelMana;
    lpObj->Life = lpObj->MaxLife;
    lpObj->Mana = lpObj->MaxMana;

    lpObj->Life = lpObj->MaxLife + lpObj->AddLife;
    lpObj->Mana = lpObj->MaxMana + lpObj->AddMana;

    gObjCalCharacter.CalcShieldPoint(lpObj);
    lpObj->iShield = lpObj->iMaxShield + lpObj->iAddShield;
    GSProtocol.GCReFillSend(lpObj->m_Index, lpObj->Life, 0xFF, 0, lpObj->iShield);
    lpObj->m_PlayerData->MasterNextExp = this->gObjNextMLExpCal(lpObj);
    gObjSetBP(lpObj->m_Index);
    gObjCalcMaxLifePower(lpObj->m_Index);
    GSProtocol.GCMasterLevelUpMsgSend(lpObj->m_Index);

    g_Log.Add("[Mastering System] MLevel Up (ML:%d) (Point:%d) (%s)(%s)", lpObj->m_PlayerData->MasterLevel,
              lpObj->m_PlayerData->MasterPoint, lpObj->AccountID, lpObj->Name);

    return false;

}

UINT64 CMasterLevelSystem::gObjNextMLExpCal(LPOBJ lpObj) {
    if (lpObj->Type != OBJ_USER)
        return 0;

    return this->gMasterExperience[lpObj->m_PlayerData->MasterLevel + 1];
}

bool CMasterLevelSystem::IsMasterLevelUser(LPOBJ lpObj) {
    if (lpObj->Type != OBJ_USER)
        return false;

    if (lpObj->m_PlayerData->ChangeUP == 2 && lpObj->Level >= g_ConfigRead.data.common.UserMaxLevel)
        return true;

    return false;
}

bool CMasterLevelSystem::CheckMLGetExp(LPOBJ lpObj, LPOBJ lpTargetObj) {
    int iMonsterMinLevel = g_ConfigRead.data.common.MLMonsterMinLevel;

    if (lpObj->Type != OBJ_USER) {
        return FALSE;
    }

    if (lpObj->m_PlayerData->VipType != 0) {
        iMonsterMinLevel = g_VipSystem.GetMLMonsterMinLevel(lpObj);
    }

    if (this->IsMasterLevelUser(lpObj)) {
        if (lpTargetObj->Level < iMonsterMinLevel) return FALSE;
    }

    return TRUE;
}

void CMasterLevelSystem::SetExpTable() {
    if (this->gMasterExperience != NULL) {
        delete[] this->gMasterExperience;
    }

    this->gMasterExperience = new UINT64[g_ConfigRead.data.common.MLUserMaxLevel + 1];

    if (this->gMasterExperience == NULL) {
        g_Log.MsgBox("error - memory allocation failed");
        return;
    }

    this->gMasterExperience[0] = 0;
    MULua *TempLua = new MULua(false);
    TempLua->DoFile(g_ConfigRead.GetPath("\\Scripts\\Misc\\ExpCalc.lua"));
    double exp = 0.0;

    for (int n = 1; n <= g_ConfigRead.data.common.MLUserMaxLevel; n++) {
        TempLua->Generic_Call("SetExpTable_Master", "ii>d", n, g_ConfigRead.data.common.UserMaxLevel, &exp);
        this->gMasterExperience[n] = exp;
        g_Log.Add("[MASTER EXP] [MASTERLEVEL %d] [MLEXP %I64d]", n, this->gMasterExperience[n]);
    }

    delete TempLua;

    g_Log.Add("Master exp setting exp table is completed");
}

void CMasterLevelSystem::SendMLData(LPOBJ lpObj) {
    if (lpObj->Type != OBJ_USER) {
        return;
    }

    PMSG_MASTER_INFO_SEND pMsg;

    PHeadSubSetB((LPBYTE) & pMsg, 0xF3, 0x50, sizeof(pMsg));

    pMsg.MasterLevel = lpObj->m_PlayerData->MasterLevel;
    pMsg.MLExpHHH = SET_NUMBERH(SET_NUMBERHW(HIDWORD(lpObj->m_PlayerData->MasterExperience)));
    pMsg.MLExpHHL = SET_NUMBERL(SET_NUMBERHW(HIDWORD(lpObj->m_PlayerData->MasterExperience)));
    pMsg.MLExpHLH = SET_NUMBERH(SET_NUMBERLW(HIDWORD(lpObj->m_PlayerData->MasterExperience)));
    pMsg.MLExpHLL = SET_NUMBERL(SET_NUMBERLW(HIDWORD(lpObj->m_PlayerData->MasterExperience)));
    pMsg.MLExpLHH = SET_NUMBERH(SET_NUMBERHW(LODWORD(lpObj->m_PlayerData->MasterExperience)));
    pMsg.MLExpLHL = SET_NUMBERL(SET_NUMBERHW(LODWORD(lpObj->m_PlayerData->MasterExperience)));
    pMsg.MLExpLLH = SET_NUMBERH(SET_NUMBERLW(LODWORD(lpObj->m_PlayerData->MasterExperience)));
    pMsg.MLExpLLL = SET_NUMBERL(SET_NUMBERLW(LODWORD(lpObj->m_PlayerData->MasterExperience)));
    pMsg.MLNextExpHHH = SET_NUMBERH(SET_NUMBERHW(HIDWORD(lpObj->m_PlayerData->MasterNextExp)));
    pMsg.MLNextExpHHL = SET_NUMBERL(SET_NUMBERHW(HIDWORD(lpObj->m_PlayerData->MasterNextExp)));
    pMsg.MLNextExpHLH = SET_NUMBERH(SET_NUMBERLW(HIDWORD(lpObj->m_PlayerData->MasterNextExp)));
    pMsg.MLNextExpHLL = SET_NUMBERL(SET_NUMBERLW(HIDWORD(lpObj->m_PlayerData->MasterNextExp)));
    pMsg.MLNextExpLHH = SET_NUMBERH(SET_NUMBERHW(LODWORD(lpObj->m_PlayerData->MasterNextExp)));
    pMsg.MLNextExpLHL = SET_NUMBERL(SET_NUMBERHW(LODWORD(lpObj->m_PlayerData->MasterNextExp)));
    pMsg.MLNextExpLLH = SET_NUMBERH(SET_NUMBERLW(LODWORD(lpObj->m_PlayerData->MasterNextExp)));
    pMsg.MLNextExpLLL = SET_NUMBERL(SET_NUMBERLW(LODWORD(lpObj->m_PlayerData->MasterNextExp)));
    pMsg.MasterPoint = lpObj->m_PlayerData->MasterPoint;
    pMsg.MaxLife = lpObj->MaxLife + lpObj->AddLife;
    pMsg.MaxMana = lpObj->MaxMana + lpObj->AddMana;
    pMsg.MaxShield = lpObj->iMaxShield + lpObj->iAddShield;
    pMsg.MaxBP = lpObj->MaxBP + lpObj->AddBP;

    IOCP.DataSend(lpObj->m_Index, (LPBYTE) & pMsg, pMsg.h.size);
}

void CMasterLevelSystem::InitData(LPOBJ lpObj) {
    if (lpObj->Type != OBJ_USER) {
        return;
    }

    lpObj->m_PlayerData->MasterLevel = 0;
    lpObj->m_PlayerData->MasterPoint = 0;
    lpObj->m_PlayerData->MasterExperience = 0;
    lpObj->m_PlayerData->MasterNextExp = this->gObjNextMLExpCal(lpObj);

    g_Log.Add("[Mastering System] [%s][%s] Set First Data after Quest", lpObj->AccountID, lpObj->Name);

    this->SendMLData(lpObj);
}

int CMasterLevelSystem::GetDieDecExpRate(LPOBJ lpObj) {
    if (this->IsMasterLevelUser(lpObj) == FALSE) {
        return -1;
    }

    int DecRate = 0;

    if (lpObj->m_PK_Level <= 3) {
        DecRate = 7;
    } else if (lpObj->m_PK_Level == 4) {
        DecRate = 20;
    } else if (lpObj->m_PK_Level == 5) {
        DecRate = 30;
    } else if (lpObj->m_PK_Level >= 6) {
        DecRate = 40;
    }

    return DecRate;
}

int CMasterLevelSystem::GetDieDecMoneyRate(LPOBJ lpObj) {
    if (this->IsMasterLevelUser(lpObj) == FALSE) {
        return -1;
    }

    return 4;
}