/****************************************************************************/
// Directory Integrity Service
//
// Copyright (C) 2000, Microsoft Corporation
/****************************************************************************/

#include "dis.h"
#include "jetrpc.h"
#include "jetsdis.h"
#include "sdevent.h"
#include "resource.h"
#include "tssdshrd.h"
#include "sdrpc.h"


#pragma warning (push, 4)

#define SERVER_ADDRESS_LENGTH 64
#define NUM_JETRPC_THREADS 10
#define MAX_DRIVE_LETTER_LENGTH 24
#define SD_QUERY_ENDPOINT_NAME L"TSSessionDirectoryQueryApi"
// Number of 100-nanosecond periods in 1 second.
#define FILETIME_INTERVAL_TO_SECONDS_MULTIPLIER 10000000

#define DEBUG_LOG_FILENAME L"tssdis.log"

#define REG_SESSION_DIRECTROY_CONTROL L"System\\CurrentControlSet\\Services\\Tssdis\\Parameters"

#define MAX_TSSERVERS_TO_RECOVER 0xFFFF
#define NO_RECOVER_WHEN_START 0
//do we recover previous jet database when starting SD
DWORD g_RecoverWhenStart = 1; 
//  if asking TS to repopulate session when it rejoins
//  RepoluateSession is set to FALSE only when tssdis is 
//  running on failover cluster and it's restarted within a time limit
BOOL  g_RepopulateSession = TRUE;
// If tssdis is not restarted within this time (3 mins), we think the db is not consistent
ULONGLONG g_TimeLimitToDeleteDB = 3 * 60 * FILETIME_INTERVAL_TO_SECONDS_MULTIPLIER;

//
// Global for fail-over cluster
//
DWORD g_dwClusterState;
// Cluster network name
WCHAR *g_ClusterNetworkName = NULL;
// Cluster account token
HANDLE g_hClusterToken = NULL;

// File Handle for the database timestamp file
HANDLE g_hTimeFile;

#define SDLOCALGROUPNAMELENGTH 64
#define SDLOCALGROUPDESLENGTH 128

const DIRCOLUMNS SessionDirectoryColumns[NUM_SESSDIRCOLUMNS] = {
    { "UserName", JET_coltypLongText, 512 },
    { "Domain", JET_coltypLongText, 254 },
    { "ServerID", JET_coltypLong, 0 },
    { "SessionID", JET_coltypLong, 0 },
    { "TSProtocol", JET_coltypLong, 0 },
    { "CreateTimeLow", JET_coltypLong, 0 },
    { "CreateTimeHigh", JET_coltypLong, 0 },
    { "DisconnectTimeLow", JET_coltypLong, 0 },
    { "DisconnectTimeHigh", JET_coltypLong, 0 },
    { "ApplicationType", JET_coltypLongText, 512 },
    { "ResolutionWidth", JET_coltypLong, 0 },
    { "ResolutionHeight", JET_coltypLong, 0 },
    { "ColorDepth", JET_coltypLong, 0 },
    { "State", JET_coltypBit, 0 },
};


const DIRCOLUMNS ServerDirectoryColumns[NUM_SERVDIRCOLUMNS] = {
    { "ServerID", JET_coltypLong, 0 },
    { "ServerAddress", JET_coltypLongText, 128 },
    { "ClusterID", JET_coltypLong, 0 },
    { "AlmostInTimeLow", JET_coltypLong, 0 },
    { "AlmostInTimeHigh", JET_coltypLong, 0 },
    { "NumberFailedPings", JET_coltypLong, 0 },
    { "SingleSessionMode", JET_coltypBit, 0 },
    { "ServerDNSName", JET_coltypLongText, 128 },
};


const DIRCOLUMNS ClusterDirectoryColumns[NUM_CLUSDIRCOLUMNS] = {
    { "ClusterID", JET_coltypLong, 0 },
    { "ClusterName", JET_coltypLongText, 128 },
    { "SingleSessionMode", JET_coltypBit, 0 },
};


JET_COLUMNID sesdircolumnid[NUM_SESSDIRCOLUMNS];
JET_COLUMNID servdircolumnid[NUM_SERVDIRCOLUMNS];
JET_COLUMNID clusdircolumnid[NUM_CLUSDIRCOLUMNS];

JET_INSTANCE g_instance = 0;

ADOConnection *g_pConnection;
HANDLE g_hStopServiceEvent;

SERVICE_STATUS g_DISStatus;
SERVICE_STATUS_HANDLE g_DISStatusHandle;

BOOL g_bDebug = FALSE;

PSID g_pSid = NULL;                    //Sid for SD local group 
PSID g_pAdminSid = NULL;               //Sid for admin on the SD server

// Registry settings follow
#if 0
DWORD g_bUseSQL = 0;
#endif


enum TraceOutputMode {
    NoTraceOutput,
    DebugPrintOutput,
    StdOutput,
    FileOutput
};

TraceOutputMode g_TraceOutputMode = NoTraceOutput;
HANDLE g_hFileOutput = INVALID_HANDLE_VALUE;

// For debugging purposes, we can set the ping mode to something other than
// WinStationOpenServer using the registry.
enum PingMode {
    NormalMode,
    AlwaysSucceed,
    AlwaysFail
};

PingMode g_PingMode = NormalMode;

ULONGLONG g_TimeServerSilentBeforePing = 60 * FILETIME_INTERVAL_TO_SECONDS_MULTIPLIER;
DWORD DISNumberSecondsBetweenPings = 10;
DWORD g_NumberFailedPingsBeforePurge = 3;


#ifdef DBG
void OutputAllTables();
#endif

void DISDeleteLocalGroupSecDes();
RPC_STATUS SDInitQueryRPC(VOID);

void TSDISErrorOut(wchar_t *format_string, ...)
{
    // Immediately bail out if we are in NoTraceOutput mode.
    if (g_TraceOutputMode == NoTraceOutput) {
        return;
    }
    else {
        // Otherwise, do the right thing.
        wchar_t TotalString[MAX_DEBUG_STRING_LENGTH + MAX_THREADIDSTR_LENGTH];
        wchar_t *ThreadIDString = TotalString;
        wchar_t *DebugOutString = NULL;
        va_list args;
        int ThreadStrLength;

        // Get the current thread ID
        ThreadStrLength = _snwprintf(ThreadIDString, MAX_THREADIDSTR_LENGTH, 
                L"%d: ", GetCurrentThreadId());

        // Set the place for the out string to after the string, or after the whole
        // buffer if _snwprintf didn't have enough space.
        if (ThreadStrLength > 0)
            DebugOutString = &TotalString[ThreadStrLength];
        else
            DebugOutString = &TotalString[MAX_THREADIDSTR_LENGTH];
            
        va_start(args, format_string);

        // Create the debug output string.
        _vsnwprintf(DebugOutString, MAX_DEBUG_STRING_LENGTH, format_string, args);
        DebugOutString[MAX_DEBUG_STRING_LENGTH - 1] = '\0';

        // Output to the correct place.
        switch (g_TraceOutputMode) {
            
        case DebugPrintOutput:
            OutputDebugString(TotalString);
            break;

        case StdOutput:
            wprintf(TotalString);
            break;

        case FileOutput:
            {
                char TotalStringA[MAX_DEBUG_STRING_LENGTH + 
                        MAX_THREADIDSTR_LENGTH];
                DWORD dwBytes = 0;

                // Convert to ANSI.
                dwBytes = WideCharToMultiByte(CP_ACP, 0, TotalString, 
                        -1, TotalStringA, MAX_DEBUG_STRING_LENGTH + 
                        MAX_THREADIDSTR_LENGTH, 0, 0);

                // Don't write the terminating NULL (3rd argument)!
                // Ignore return value.
                WriteFile(g_hFileOutput, TotalStringA, dwBytes - 1, 
                        &dwBytes, NULL);
                
                break;
            }
        }

        va_end(args);
    }
}


// TSDISErrorTimeOut
//
// This function is used to output a single FILETIME low, high pair.  The format
// string, given as the first argument, MUST specify a %s format specifier for
// where the date/time should go.
//
// Example:
//  TSDISErrorTimeOut(L"The date and time are %s\n", CurrTimeLow, CurrTimeHigh);
void TSDISErrorTimeOut(wchar_t *format_string, DWORD TimeLow, DWORD TimeHigh)
{
    if (g_TraceOutputMode == NoTraceOutput) {
        return;
    }
    else {
        // We just need to convert the FILETIME we have into a SYSTEMTIME,
        // and then output the SYSTEMTIME using GetDateFormat and GetTimeFormat.
        FILETIME ft;
        SYSTEMTIME st;
        SYSTEMTIME stloc;
        int offset = 0;
        wchar_t DateString[MAX_DATE_TIME_STRING_LENGTH];

        ft.dwLowDateTime = TimeLow;
        ft.dwHighDateTime = TimeHigh;

        if (FileTimeToSystemTime(&ft, &st) != 0) {
            // st is the system time.

            // UTC format?
            if (SystemTimeToTzSpecificLocalTime(NULL, &st, &stloc) != 0) {
                offset = GetDateFormat(LOCALE_SYSTEM_DEFAULT, DATE_SHORTDATE, 
                        &stloc, NULL, DateString, MAX_DATE_TIME_STRING_LENGTH);

                if (offset != 0) {
                    // Turn the terminating NULL into a space.
                    DateString[offset - 1] = ' ';
                    
                    // Write the time after the space.
                    offset = GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, &stloc, 
                            NULL, &DateString[offset], 
                            MAX_DATE_TIME_STRING_LENGTH - offset);

                    if (offset != 0) {
                        // Output the string.
                        TSDISErrorOut(format_string, DateString);
                    }
                }
            }
        }
    }
}


// This function is duplicated from \nt\termsrv\winsta\server\sessdir.cpp.
//
// PostSessDirErrorValueEvent
//
// Utility function used to create a system log wType event containing one
// hex DWORD code value.
void PostSessDirErrorValueEvent(unsigned EventCode, DWORD ErrVal, WORD wType)
{
    HANDLE hLog;
    WCHAR hrString[128];
    PWSTR String = NULL;
    static DWORD numInstances = 0;
    //
    //count the numinstances of out of memory error, if this is more than
    //a specified number, we just won't log them
    //
    if( MY_STATUS_COMMITMENT_LIMIT == ErrVal )
    {
        if( numInstances > MAX_INSTANCE_MEMORYERR )
            return;
         //
        //if applicable, tell the user that we won't log any more of the out of memory errors
        //
        if( numInstances >= MAX_INSTANCE_MEMORYERR - 1 ) {
            wsprintfW(hrString, L"0x%X. This type of error will not be logged again to avoid eventlog fillup.", ErrVal);
            String = hrString;
        }
        numInstances++;
    }

    hLog = RegisterEventSource(NULL, L"TermServSessDir");
   if (hLog != NULL) {
        if( NULL == String ) {
            wsprintfW(hrString, L"0x%X", ErrVal);
            String = hrString;
        }
        ReportEvent(hLog, wType, 0, EventCode, NULL, 1, 0,
                (const WCHAR **)&String, NULL);
        DeregisterEventSource(hLog);
    }
}

// PostSessDirErrorMsgEvent
//
// Utility function used to create a system log wType event containing one
// WCHAR msg.
void PostSessDirErrorMsgEvent(unsigned EventCode, WCHAR *szMsg, WORD wType)
{
    HANDLE hLog;
    
    hLog = RegisterEventSource(NULL, L"TermServSessDir");
    if (hLog != NULL) {
        ReportEvent(hLog, wType, 0, EventCode, NULL, 1, 0,
                (const WCHAR **)&szMsg, NULL);
        DeregisterEventSource(hLog);
    }
}


// DISJetGetServersPendingReconnects
//
// Returns arrays of max length 10 of servers pending reconnects, where the
// reconnect is greater than g_TimeServerSilentBeforePing seconds.
HRESULT STDMETHODCALLTYPE DISJetGetServersPendingReconnects(
        OUT long __RPC_FAR *pNumSessionsReturned,
        OUT WCHAR ServerAddressRows[10][SERVER_ADDRESS_LENGTH],
        OUT DWORD ServerIDs[10])
{
    JET_ERR err;
    JET_SESID sesid = JET_sesidNil;
    JET_DBID dbid;
    JET_TABLEID servdirtableid;
    DWORD zero = 0;
    *pNumSessionsReturned = 0;
    unsigned i = 0;
    unsigned long cbActual;
    // These are really FILETIMEs, but we want to do 64-bit math on them,
    // and they're the same structure as FILETIMEs.
    ULARGE_INTEGER ulCurrentTime;
    ULARGE_INTEGER ulAITTime;
        
    //TSDISErrorOut(L"GetPendRec...");
    CALL(JetBeginSession(g_instance, &sesid, "user", ""));
    CALL(JetOpenDatabase(sesid, JETDBFILENAME, "", &dbid, 0));

    CALL(JetOpenTable(sesid, dbid, "ServerDirectory", NULL, 0, 0, 
            &servdirtableid));

    // Get the current file time.
    SYSTEMTIME st;
    
    // Retrieve the time.
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, (FILETIME *) &ulCurrentTime);

    // Set the current time to the sd timestamp file    
    SetFileTime(g_hTimeFile, NULL, NULL, (FILETIME *)&ulCurrentTime);

    CALL(JetBeginTransaction(sesid));
    
    // Since Jet has no unsigned long type, go through the servers first
    // looking for keys greater than 0, 0, then looking for keys less than 0, 0
    // TODO: Consider how to do this with JET_coltypDateTime or using NULLs
    for (int j = 0; j < 2; j++) {
        CALL(JetSetCurrentIndex(sesid, servdirtableid, "ServerAlmostInTimes"));

        CALL(JetMakeKey(sesid, servdirtableid, &zero, sizeof(zero), 
                JET_bitNewKey));
        CALL(JetMakeKey(sesid, servdirtableid, &zero, sizeof(zero), 0));

        if (0 == j)
            err = JetSeek(sesid, servdirtableid, JET_bitSeekGT);
        else
            err = JetSeek(sesid, servdirtableid, JET_bitSeekLT);

        while ((i < TSSD_MaxDisconnectedSessions) && (JET_errSuccess == err)) {

            // Get AlmostInTimeLow, AlmostInTimeHigh (3 + 4) for computation.
            CALL(JetRetrieveColumn(sesid, servdirtableid, servdircolumnid[
                    SERVDIR_AITLOW_INTERNAL_INDEX], &(ulAITTime.LowPart), 
                    sizeof(ulAITTime.LowPart), &cbActual, 0, NULL));
            CALL(JetRetrieveColumn(sesid, servdirtableid, servdircolumnid[
                    SERVDIR_AITHIGH_INTERNAL_INDEX], &(ulAITTime.HighPart), 
                    sizeof(ulAITTime.HighPart), &cbActual, 0, NULL));

            // If the difference between the current time and the time the
            // server was stamped is greater than the set 
            // TimeServerSilentBeforePing, then put it in the return array, 
            // else don't.
            if ((ulCurrentTime.QuadPart - ulAITTime.QuadPart) > 
                    g_TimeServerSilentBeforePing) {

                // Get ServerID
                CALL(JetRetrieveColumn(sesid, servdirtableid, servdircolumnid[
                        SERVDIR_SERVID_INTERNAL_INDEX], &ServerIDs[i], 
                        sizeof(ServerIDs[i]), &cbActual, 0, NULL));

                // Get the ServerAddress for this record.
                CALL(JetRetrieveColumn(sesid, servdirtableid, servdircolumnid[
                        SERVDIR_SERVADDR_INTERNAL_INDEX], 
                        &ServerAddressRows[i][0], sizeof(ServerAddressRows[i]),
                        &cbActual, 0, NULL));

                i += 1;
            }

            // Move to the next matching record.
            if (0 == j)
                err = JetMove(sesid, servdirtableid, JET_MoveNext, 0);
            else
                err = JetMove(sesid, servdirtableid, JET_MovePrevious, 0);
        }
    }

    *pNumSessionsReturned = i;
    
    CALL(JetCommitTransaction(sesid, 0));

    CALL(JetCloseTable(sesid, servdirtableid));

    CALL(JetCloseDatabase(sesid, dbid, 0));

    CALL(JetEndSession(sesid, 0));

    return S_OK;

HandleError:
    if (sesid != JET_sesidNil) {
        // Can't really recover.  Just bail out.
        (VOID) JetRollback(sesid, JET_bitRollbackAll);

        // Force the session closed
        (VOID) JetEndSession(sesid, JET_bitForceSessionClosed);
    }
    
    return E_FAIL;
}


#if 0
HRESULT STDMETHODCALLTYPE DISSQLGetServersPendingReconnects(
        OUT long __RPC_FAR *pNumSessionsReturned, 
        OUT CVar *pVarRows)
{
    long NumRecords = 0;
    HRESULT hr;
    ADOCommand *pCommand;
    ADOParameters *pParameters;
    ADORecordset *pResultRecordSet;
    CVar varFields;
    CVar varStart;

    TRC2((TB,"GetServersWithDisconnectedSessions"));

    ASSERT((pNumSessionsReturned != NULL),(TB,"NULL pNumSess"));

    hr = CreateADOStoredProcCommand(L"SP_TSDISGetServersPendingReconnects",
            &pCommand, &pParameters);
    if (SUCCEEDED(hr)) {
        // Execute the command.
        hr = pCommand->Execute(NULL, NULL, adCmdStoredProc,
                &pResultRecordSet);

        pParameters->Release();
        pCommand->Release();
    }
    else {
        ERR((TB,"GetServersWDiscSess: Failed create cmd, hr=0x%X", hr));
    }
        
    // At this point we have a result recordset containing the server rows
    // corresponding to all of the disconnected sessions.
    if (SUCCEEDED(hr)) {
        long State;

        NumRecords = 0;

        hr = pResultRecordSet->get_State(&State);
        if (SUCCEEDED(hr)) {
            if (!(State & adStateClosed)) {
                VARIANT_BOOL VB;

                // If EOF the recordset is empty.
                hr = pResultRecordSet->get_EOF(&VB);
                if (SUCCEEDED(hr)) {
                    if (VB) {
                        TRC1((TB,"GetServersWDiscSess: Result recordset EOF, "
                                "0 rows"));
                        goto PostUnpackResultSet;
                    }
                }
                else {
                    ERR((TB,"GetServersWDiscSess: Failed get_EOF, hr=0x%X", 
                            hr));
                    goto PostUnpackResultSet;
                }
            }
            else {
                ERR((TB,"GetServersWDiscSess: Closed result recordset"));
                goto PostUnpackResultSet;
            }
        }
        else {
            ERR((TB,"GetServersWDiscSess: get_State failed, hr=0x%X", hr));
            goto PostUnpackResultSet;
        }
        
        // Grab the result data into a safearray, starting with the default
        // current row and all fields.
        varStart.InitNoParam();
        varFields.InitNoParam();
        hr = pResultRecordSet->GetRows(adGetRowsRest, varStart,
                varFields, pVarRows);
        if (SUCCEEDED(hr)) {
            hr = SafeArrayGetUBound(pVarRows->parray, 2, &NumRecords);
            if (SUCCEEDED(hr)) {
                // 0-based array bound was returned, num rows is that + 1.
                NumRecords++;

                TRC1((TB,"%d rows retrieved from safearray", NumRecords));
            }
            else {
                ERR((TB,"GetServersWithDisc: Failed safearray getubound, "
                        "hr=0x%X", hr));
                goto PostUnpackResultSet;
            }
        }
        else {
            ERR((TB,"GetServersWDiscSess: Failed to get rows, hr=0x%X", hr));
            goto PostUnpackResultSet;
        }


PostUnpackResultSet:
        pResultRecordSet->Release();
    }
    else {
        ERR((TB,"GetServersWDiscSess: Failed exec, hr=0x%X", hr));
    }

    *pNumSessionsReturned = NumRecords;
    return hr;
}
#endif


/****************************************************************************/
// DISDebugControlHandler
//
// Handle console control events for when service is in debug mode.
/****************************************************************************/
BOOL WINAPI DISDebugControlHandler(DWORD dwCtrlType) {

    switch(dwCtrlType)
    {
    case CTRL_BREAK_EVENT:
    case CTRL_C_EVENT:
        TSDISErrorOut(L"Stopping service\n");

        SetEvent(g_hStopServiceEvent);
        // Should I wait for that to complete?

        return TRUE;
        break;
    }
    return FALSE;
}


/****************************************************************************/
// DISPingServer
//
// Given the IP address of a server, pings it.  Returns TRUE on success, FALSE 
// on failure.
/****************************************************************************/
BOOLEAN DISPingServer(WCHAR *ServerAddress) {
    HANDLE hServer = NULL;
    hServer = WinStationOpenServer(ServerAddress);

    // The only case where we return false is where hServer is NULL and the
    // reason is not ERROR_ACCESS_DENIED.
    if (hServer == NULL) {
        if (GetLastError() != ERROR_ACCESS_DENIED)
            return FALSE;
    }
    else {
        // The hServer is valid, so clean up.
        WinStationCloseServer(hServer);
    }
    return TRUE;
}


/****************************************************************************/
// DISGetServerStatus
//
// Given the IP address of a server, determines its state (Responding or 
// NotResponding).
// 
// Currently implemented as a ping.  See lengthy comment in main for one
// possible future optimization.
/****************************************************************************/
SERVER_STATUS DISGetServerStatus(WCHAR *ServerAddress) {

    switch (g_PingMode) {

    case AlwaysFail:
        return NotResponding;

    case AlwaysSucceed:
        return Responding;

    case NormalMode:
        // NOTE INTENTIONAL FALLTHROUGH.
    default:
        if (DISPingServer(ServerAddress) == TRUE)
            return Responding;
        else
            return NotResponding;

    }

}


#if 0
HRESULT DISSQLInitialize() {
    // Retrieve number of seconds to wait from the registry -- NOT IMPLEMENTED
    HRESULT hr = S_OK;
    BSTR ConnectString = NULL;
    LONG RegRetVal;
    HKEY hKey;
    BSTR ConnectStr = NULL;
    BSTR UserStr = NULL;
    BSTR PwdStr = NULL;

    RegRetVal = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
            TEXT("Software\\Microsoft\\DIS"), 0, KEY_READ, &hKey);
    if (RegRetVal == ERROR_SUCCESS) {
        DWORD Type, DataSize;

        // Determine the needed size.
        DataSize = 0;
        RegRetVal = RegQueryValueExW(hKey, L"ConnectString", NULL,
                &Type, NULL, &DataSize);
        DataSize &= ~1;
        if (RegRetVal == ERROR_SUCCESS && Type == REG_SZ) {
            ConnectString = SysAllocStringLen(L"", DataSize /
                    sizeof(WCHAR));
            if (ConnectString != NULL) {
                RegRetVal = RegQueryValueExW(hKey, L"ConnectString",
                        NULL, &Type, (BYTE *)ConnectString,
                        &DataSize);
                if (RegRetVal == ERROR_SUCCESS) {
                    // Hold onto the connect string for use below.
                    TRC1((TB,"Retrieved conn str %S", ConnectString));
                }
                else {
                    ERR((TB,"Final RegQuery failed, err=%u", RegRetVal));
                    hr = E_FAIL;
                    goto Cleanup;
                }
            }
            else {
                ERR((TB,"Failed alloc connect string"));
                hr = E_OUTOFMEMORY;
                goto Cleanup;
            }
        }
        else {
            ERR((TB,"Failed RegQuery - err=%u, DataSize=%u, type=%u",
                    RegRetVal, DataSize, Type));
            hr = E_FAIL;
            goto Cleanup;
        }

        RegCloseKey(hKey);
    }
    else {
        ERR((TB,"RegOpenKeyEx returned err %u", RegRetVal));
        hr = E_FAIL;
        goto Cleanup;
    }

    hr = CoInitialize(NULL);

    // Alloc the BSTRs for the connection.
    ConnectStr = SysAllocString(ConnectString);
    UserStr = SysAllocString(L"");
    PwdStr = SysAllocString(L"");

    if ((ConnectStr == NULL) || (UserStr == NULL) || (PwdStr == NULL)) {
        ERR((TB, "Failed alloc Connect, User, or PwdStr"));
        hr = E_OUTOFMEMORY;
        goto Cleanup;
    }

    // Create an ADO connection instance and connect.
    hr = CoCreateInstance(CLSID_CADOConnection, NULL,
            CLSCTX_INPROC_SERVER, IID_IADOConnection,
            (LPVOID *)&g_pConnection);
    if (SUCCEEDED(hr)) {
        // Do the open.
        hr = g_pConnection->Open(ConnectStr, UserStr, PwdStr,
                adOpenUnspecified);
        if (!SUCCEEDED(hr)) {
            ERR((TB,"Failed open DB, hr=0x%X", hr));
            g_pConnection->Release();
            g_pConnection = NULL;
        }
    }
    else {
        ERR((TB,"CoCreate(ADOConn) returned 0x%X", hr));
    }

Cleanup:

    // SysFreeString(NULL) is ok.
    SysFreeString(ConnectString);
    SysFreeString(ConnectStr);
    SysFreeString(UserStr);
    SysFreeString(PwdStr);

    return hr;
}
#endif


// Call each TS Server to ask them to rejoin SD
void __cdecl DISOpenServer(void *Para)
{
    HRESULT hr;
    WCHAR *pBindingString = NULL;
    RPC_BINDING_HANDLE hRPCBinding = NULL;
    WCHAR *szPrincipalName = NULL;
    WCHAR *ServerName = NULL;
    SDRecoverServerNames *SDRRecoverServerPara = (SDRecoverServerNames *)Para;
    unsigned int count = SDRRecoverServerPara->count;
    WCHAR ** ServerNameArray = SDRRecoverServerPara->ServerNameArray;
    unsigned int i;
    unsigned long RpcException;
    DWORD dwRejoinFlag = 0;

    dwRejoinFlag |= TSSD_FORCEREJOIN;

    // Impersonate the cluster account to make the rejoin RPC call
    if (g_dwClusterState == ClusterStateRunning) {
        if (g_hClusterToken) {
            if(!ImpersonateLoggedOnUser(g_hClusterToken)) {
                // If we failed to impersonate the cluster account, don't ask TS to rejoin, since it will 
                //  fail anyway due to access denied
                TSDISErrorOut(L"SD Recover: Error %d in ImpersonateLoggedOnUser\n", GetLastError());
                goto HandleError;
            }
        }
        else {
            // If g_hClusterToken is NULL, don't ask TS to rejoin, since it will 
            //  fail anyway due to access denied
            goto HandleError;
        }
    }

    // If it's on failover cluster, set the flag to tell server not to repopulate its sessions
    if (g_RepopulateSession == FALSE) {
        dwRejoinFlag |= TSSD_NOREPOPULATE;
    }

    for (i=0;i<count;i++) {
        if (NULL != hRPCBinding) {
            RpcBindingFree(&hRPCBinding);
            hRPCBinding = NULL;
        }
    
        ServerName = *(ServerNameArray + i);
        // Connect to the tssdjet RPC server according to the server name provided.
        // We first create an RPC binding handle from a composed binding string.
        hr = RpcStringBindingCompose(/*(WCHAR *)g_RPCUUID,*/
                0,
                L"ncacn_ip_tcp", ServerName,
                0,
                NULL, &pBindingString);

        if (hr == RPC_S_OK) {
            // Generate the RPC binding from the canonical RPC binding string.
            hr = RpcBindingFromStringBinding(pBindingString, &hRPCBinding);
            if (hr != RPC_S_OK) {
                ERR((TB,"SD Recover: Error %d in RpcBindingFromStringBinding\n", hr));
                goto LogError;
            } 
        }
        else {
            ERR((TB,"SD Recover: Error %d in RpcStringBindingCompose\n", hr));
            goto LogError;
        }

        hr = RpcEpResolveBinding(hRPCBinding, TSSDTOJETRPC_ClientIfHandle);
        if (hr != RPC_S_OK) {
            ERR((TB, "SD Recover: Error %d in RpcEpResolveBinding", hr));
            goto LogError;
        }

        hr = RpcMgmtInqServerPrincName(hRPCBinding,
                                       RPC_C_AUTHN_GSS_NEGOTIATE,
                                       &szPrincipalName);
        if (hr != RPC_S_OK) {
            ERR((TB,"SD Recover: Error %d in RpcMgmtIngServerPrincName", hr));
            goto LogError;
        }

        hr = RpcBindingSetAuthInfo(hRPCBinding,
                                szPrincipalName,
                                RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                                RPC_C_AUTHN_GSS_NEGOTIATE,
                                NULL,  //CurrentIdentity
                                NULL);
        RpcStringFree(&szPrincipalName);

        if (hr != RPC_S_OK) {
            ERR((TB,"SD Recover: Error %d in RpcBindingSetAuthInfo", hr));
            goto LogError;
        } 

        RpcTryExcept {
            // Make the call to TS to ask it to rejoin
            hr = TSSDRPCRejoinSD(hRPCBinding, dwRejoinFlag);
        }
        RpcExcept(TSSDRpcExceptionFilter(RpcExceptionCode())) {
            RpcException = RpcExceptionCode();
            hr = RpcException;
            ERR((TB,"ForceRejoin: RPC Exception %d\n", RpcException));
        }
        RpcEndExcept
                
LogError:
        if (hr != RPC_S_OK) {
            PostSessDirErrorMsgEvent(EVENT_FAIL_CALL_TS_REJOIN, ServerName, EVENTLOG_ERROR_TYPE);
        }
    }

    // Stop Impersonating
    if (g_dwClusterState == ClusterStateRunning) {
        RevertToSelf();
    }

HandleError:

    if (NULL != hRPCBinding) {
        RpcBindingFree(&hRPCBinding);
        hRPCBinding = NULL;
    }
    // Free 
    for (i=0;i<count;i++) {
        LocalFree(*(ServerNameArray + i));
    }
    LocalFree(ServerNameArray);
    LocalFree(SDRRecoverServerPara);
    
    return;
}

// When SD service is restarted, try to recover the Servers in the SD
// Jet database and ask them to rejoin SD
BOOL DISJetRecover()
{
    JET_SESID sesid = JET_sesidNil;;
    JET_TABLEID servdirtableid;
    JET_DBID dbid = JET_dbidNil;
    JET_COLUMNDEF jcd;
    JET_COLUMNID ServerAddrCId;
    JET_ERR err = JET_errSuccess;
    unsigned long i, count = 0;
    WCHAR ServerName[SERVER_ADDRESS_LENGTH];
    WCHAR **ServerNameArray = NULL;
    unsigned long cbActual;
    SDRecoverServerNames *pSDRecoverServerPara;

    CALL(JetSetSystemParameter(&g_instance, 0, JET_paramSystemPath, 
            0, JETDISDBDIRECTORY));
    CALL(JetSetSystemParameter(&g_instance, 0, JET_paramTempPath,
            0, JETDISDBDIRECTORY));
    CALL(JetSetSystemParameter(&g_instance, 0, JET_paramLogFilePath,
            0, JETDISDBDIRECTORY));
    CALL(JetSetSystemParameter(&g_instance, 0, JET_paramCircularLog,
            1, NULL));
    
    CALL(JetInit(&g_instance));
    CALL(JetBeginSession(g_instance, &sesid, "user", ""));

        

    CALL(JetAttachDatabase(sesid, JETDBFILENAME, 0));

    // Populate our columnid arrays
    CALL(JetOpenDatabase(sesid, JETDBFILENAME, "", &dbid, 0));

    CALL(JetOpenTable(sesid, dbid, "ServerDirectory", NULL, 0, 0, 
                &servdirtableid));

    CALL(JetBeginTransaction(sesid));

    CALL(JetGetColumnInfo(sesid, dbid, "ServerDirectory",
                    ServerDirectoryColumns[SERVDIR_SERVADDR_INTERNAL_INDEX].szColumnName, &jcd,
                    sizeof(jcd), 0));
    ServerAddrCId = jcd.columnid;

    pSDRecoverServerPara = (SDRecoverServerNames *)LocalAlloc(LMEM_FIXED, sizeof(SDRecoverServerNames));
    if (NULL == pSDRecoverServerPara) {
        goto HandleError;
    }
    // Get number of TS Servers in the SD
    err = JetIndexRecordCount(sesid, servdirtableid, &count, MAX_TSSERVERS_TO_RECOVER);

    if (err != JET_errSuccess)
        goto HandleError;
    if (count) {
        CALL(JetMove(sesid, servdirtableid, JET_MoveFirst, 0));
        ServerNameArray = (WCHAR **)LocalAlloc(LMEM_FIXED, count * sizeof(WCHAR *));
        if (NULL == ServerNameArray) {
            goto HandleError;
        }
    }
    TSDISErrorOut(L"We have %d Servers to recover\n", count);
    for(i=0;i<count;i++)
    {
        CALL(JetRetrieveColumn(sesid, servdirtableid, ServerAddrCId,
                          ServerName, SERVER_ADDRESS_LENGTH, &cbActual, 0, NULL));
        TSDISErrorOut(L"Server %d is %s\n", i+1, ServerName);

        *(ServerNameArray + i) = (WCHAR *)LocalAlloc(LMEM_FIXED, sizeof(ServerName));
        if (NULL == *(ServerNameArray+i)) {
            goto HandleError;
        }
        memcpy((BYTE *)(*(ServerNameArray + i)), (BYTE*)ServerName, sizeof(ServerName));

        if (i != (count-1))
            CALL(JetMove(sesid, servdirtableid, JET_MoveNext, 0));
    }
    // Spin a thread to call TS servers to rejoin SD
    pSDRecoverServerPara->count = count;
    pSDRecoverServerPara->ServerNameArray = ServerNameArray;
    if(-1 == _beginthread(DISOpenServer, 0, (PVOID)pSDRecoverServerPara)) {
        TSDISErrorOut(L"Unable to begin DISOpenServer thread\n");
        // Free mem
        for (i=0;i<count;i++) {
            LocalFree(*(pSDRecoverServerPara->ServerNameArray + i));
        }
        LocalFree(pSDRecoverServerPara->ServerNameArray);
        LocalFree(pSDRecoverServerPara);

    }

    CALL(JetCommitTransaction(sesid, 0));


    CALL(JetCloseTable(sesid, servdirtableid));

    CALL(JetCloseDatabase(sesid, dbid, 0));

    CALL(JetEndSession(sesid, 0));

    CALL(JetTerm(g_instance));
    g_instance = 0;

    return TRUE;

HandleError: 
    if (sesid != JET_sesidNil) {
        // Can't really recover.  Just bail out.
        (VOID) JetRollback(sesid, JET_bitRollbackAll);

        // Force the session closed
        (VOID) JetEndSession(sesid, JET_bitForceSessionClosed);
    }
    
    JetTerm(g_instance);
    g_instance = 0;

    return FALSE;
}

// Delete the database and all other JET files (if present)
void DeleteJetFiles()
{
    HANDLE hFileFind;
    WIN32_FIND_DATA FindFileData;
    WCHAR filename[MAX_LOGFILE_LENGTH];
    DWORD dwError;

    // Delete the database and all other JET files (if present), and start anew.
    (void) DeleteFile(JETDBFILENAMEW);
    (void) DeleteFile(JETAUXFILENAME1W);
    (void) DeleteFile(JETAUXFILENAME2W);
    (void) DeleteFile(JETAUXFILENAME3W);
    (void) DeleteFile(JETAUXFILENAME4W);
    (void) DeleteFile(JETAUXFILENAME5W);
    (void) DeleteFile(JETAUXFILENAME6W);

    // Delete numbered log files.  Jet can create a bunch of log files
    // of the form edb00001.log, edb00002.log, . . ., edb0000a.log,
    // edb0000b.log, . . ., edb0000f.log, edb00010.log, . . .
    hFileFind = FindFirstFile(JETLOGFILENAME, &FindFileData);
    if (hFileFind != INVALID_HANDLE_VALUE) {
        swprintf(filename, JETDISDBDIRECTORYW);
        wcsncat(filename, FindFileData.cFileName, MAX_LOGFILE_LENGTH - sizeof(JETDISDBDIRECTORYW) / sizeof(WCHAR) - 1);
        if (DeleteFile(filename) == 0) {
            dwError = GetLastError();
            if (dwError != ERROR_FILE_NOT_FOUND) {
                PostSessDirErrorValueEvent(EVENT_PROBLEM_DELETING_LOGS, 
                        dwError, EVENTLOG_ERROR_TYPE);
            }
        }
        while (FindNextFile(hFileFind, &FindFileData)) {
            swprintf(filename, JETDISDBDIRECTORYW);
            wcsncat(filename, FindFileData.cFileName, MAX_LOGFILE_LENGTH - sizeof(JETDISDBDIRECTORYW) / sizeof(WCHAR) - 1);
            if (DeleteFile(filename) == 0) {
                dwError = GetLastError();
                if (dwError != ERROR_FILE_NOT_FOUND) {
                    PostSessDirErrorValueEvent(EVENT_PROBLEM_DELETING_LOGS, 
                            dwError, EVENTLOG_ERROR_TYPE);
                    break;
                }
            }
        }

        FindClose(hFileFind);
    }
}

//
// Session directory initialization on fail-over cluster
//
// Return True on success
//
BOOL DISJetInitInCluster()
{
    BOOL fRet = FALSE;
    DWORD dwError;
    HCLUSTER hclus = NULL;
    HRESOURCE hrSD = NULL;
    WCHAR *pszDriveLetter = NULL;
    DWORD cchDriveLetter = MAX_DRIVE_LETTER_LENGTH;
    HCLUSENUM hClusEnum = NULL;
    DWORD dwIndex, dwCount;
    WCHAR ResourceName[256], *ServiceName;
    DWORD dwSize, dwType;
    LPVOID pPropertyList = NULL;
    DWORD rc;
    BOOL bFindSDService = FALSE;
    HRESOURCE hrNetworkName = NULL;
    struct CLUS_NETNAME_VS_TOKEN_INFO VsTokenInfo;
    DWORD dwReturnSize = 0;
    HANDLE hVSToken = NULL;

    // Change the current directory to the right place on the shared
    // drive.

    // Open the cluster.
    hclus = OpenCluster(NULL);

    if (hclus == NULL) {
        // TODO: Log event.
        TSDISErrorOut(L"Unable to open cluster, error %d\n", 
                      GetLastError());
        goto HandleError;
    }

    // Enuerate all the resources in the cluster to find the generic service
    // resource named "tssdis" i.e. the session directory service
    hClusEnum = ClusterOpenEnum(hclus, CLUSTER_ENUM_RESOURCE);
    if (hClusEnum == NULL) {
        // TODO: Log event.
        TSDISErrorOut(L"Unable to open cluster enum, error %d\n",
                      GetLastError());
        goto HandleError;
    }
    dwCount = ClusterGetEnumCount(hClusEnum);

    for (dwIndex=0; dwIndex<dwCount; dwIndex++) {
        if (pPropertyList != NULL) {
            LocalFree(pPropertyList);
            pPropertyList = NULL;
        }
        if (hrSD != NULL) {
            CloseClusterResource(hrSD);
            hrSD = NULL;
        }

        dwSize = sizeof(ResourceName) / sizeof(WCHAR);
        if (ClusterEnum(hClusEnum, dwIndex, &dwType, 
                        ResourceName, &dwSize) != ERROR_SUCCESS) {
            TSDISErrorOut(L"ClusterEnum fails with error %d\n",
                          GetLastError());
            continue;
        }
        hrSD = OpenClusterResource(hclus, ResourceName);
        if (hrSD == NULL) {
            TSDISErrorOut(L"OpenClusterResource fails with error %d\n",
                          GetLastError());
            continue;
        }
        pPropertyList = NULL;
        dwSize = 0;
        rc = ClusterResourceControl(hrSD,                                           // hResource
                                    NULL,                                           // hHostNode
                                    CLUSCTL_RESOURCE_GET_PRIVATE_PROPERTIES,        // dwControlCode
                                    NULL,                                           // lpInBuffer
                                    0,                                              // cbInBufferSize
                                    NULL,                                           // lpOutBuffer
                                    0,                                              // cbOutBufferSiz 
                                    &dwSize);                                       // lpcbByteReturned


        if (rc != ERROR_SUCCESS) {
            TSDISErrorOut(L"ResourceControl fails with error %d\n", rc);
            continue;
        }
        dwSize += sizeof(WCHAR);
        pPropertyList = LocalAlloc(LMEM_FIXED, dwSize);
        if (pPropertyList == NULL) {
            TSDISErrorOut(L"Can't allocate memory for propertylist with size %d\n", dwSize);
            continue;
        }
        rc = ClusterResourceControl(hrSD, 
                                    NULL, 
                                    CLUSCTL_RESOURCE_GET_PRIVATE_PROPERTIES, 
                                    NULL, 
                                    0, 
                                    pPropertyList, 
                                    dwSize,
                                    NULL);
        if (rc != ERROR_SUCCESS) {
            TSDISErrorOut(L"ResourceControl fails with error %d\n", rc);
            continue;
        }

        rc = ResUtilFindSzProperty(pPropertyList, dwSize, L"ServiceName", &ServiceName);
        if (rc == ERROR_SUCCESS) {
            if (_wcsicmp(ServiceName, L"tssdis") == 0) {
                TSDISErrorOut(L"Find tssdis resource\n");
                bFindSDService = TRUE;
                LocalFree(ServiceName);
                break;
            }
            if (ServiceName != NULL) {
                LocalFree(ServiceName);
                ServiceName = NULL;
            }
        }

        CloseClusterResource(hrSD);
        hrSD = NULL;
    }
    if (pPropertyList != NULL) {
        LocalFree(pPropertyList);
        pPropertyList = NULL;
    }
    ClusterCloseEnum(hClusEnum);

    // Bail out if can't find tssdis resource
    if (!bFindSDService) {
        // TODO: Log event.
        TSDISErrorOut(L"Unable to find the resource with service name tssdis\n");
        goto HandleError;
    }

    // Find the network name resource
    hrNetworkName = ResUtilGetResourceDependency(hrSD, L"Network Name");
    if (hrNetworkName == NULL) {
        TSDISErrorOut(L"Unable to get the dependent NetworkName resource, error is %d\n", GetLastError());
        goto HandleError;
    }

    pPropertyList = NULL;
    dwSize = 0;
    // Get the property of the network name resource
    // This is the 1st call, just get the size of the porperty list
    rc = ClusterResourceControl(hrNetworkName,                                  // hResource
                                NULL,                                           // hHostNode
                                CLUSCTL_RESOURCE_GET_PRIVATE_PROPERTIES,        // dwControlCode
                                NULL,                                           // lpInBuffer
                                0,                                              // cbInBufferSize
                                NULL,                                           // lpOutBuffer
                                0,                                              // cbOutBufferSiz 
                                &dwSize);                                       // lpcbByteReturned


    if (rc != ERROR_SUCCESS) {
        TSDISErrorOut(L"ResourceControl fails with error %d\n", rc);
        goto HandleError;
    }
    dwSize += sizeof(WCHAR);
    pPropertyList = LocalAlloc(LMEM_FIXED, dwSize);
    if (pPropertyList == NULL) {
        TSDISErrorOut(L"Can't allocate memory for propertylist with size %d\n", dwSize);
        goto HandleError;
    }
    // Get the property of the network name resource
    rc = ClusterResourceControl(hrNetworkName, 
                                NULL, 
                                CLUSCTL_RESOURCE_GET_PRIVATE_PROPERTIES, 
                                NULL, 
                                0, 
                                pPropertyList, 
                                dwSize,
                                NULL);
    if (rc != ERROR_SUCCESS) {
        TSDISErrorOut(L"ResourceControl fails with error %d\n", rc);
        goto HandleError;
    }

    // Find the "name" propery in the property list
    rc = ResUtilFindSzProperty(pPropertyList, dwSize, L"Name", &g_ClusterNetworkName);
    if (rc != ERROR_SUCCESS) {
        g_ClusterNetworkName = NULL;
        TSDISErrorOut(L"ResUtilFindSzProperty fails with error %d\n", rc);
        goto HandleError;
    }
    if (pPropertyList != NULL) {
        LocalFree(pPropertyList);
        pPropertyList = NULL;
    }

    VsTokenInfo.ProcessID = GetCurrentProcessId();
    VsTokenInfo.DesiredAccess = 0;
    VsTokenInfo.InheritHandle = FALSE;

    // Get the token of the virtual server
    rc = ClusterResourceControl(
                       hrNetworkName,
                       0,
                       CLUSCTL_RESOURCE_NETNAME_GET_VIRTUAL_SERVER_TOKEN,
                       &VsTokenInfo,
                       sizeof(CLUS_NETNAME_VS_TOKEN_INFO),
                       &hVSToken,
                       sizeof(HANDLE),
                       &dwReturnSize
                       );
    if (rc != ERROR_SUCCESS) {
        TSDISErrorOut(L"Get the virtual server token failed with error %d\n", rc);
        hVSToken = NULL;
        goto HandleError;
    }

    // Duplicate the virtual server token
    if(!DuplicateTokenEx(
            hVSToken,
            MAXIMUM_ALLOWED,
            NULL,
            SecurityImpersonation,
            TokenImpersonation,
            &g_hClusterToken)) {
        TSDISErrorOut(L"DuplicateTokenEx failed with error %d\n", GetLastError());
        CloseHandle(hVSToken);
        hVSToken = NULL;
        g_hClusterToken = NULL;

        goto HandleError;
    }
    if (hVSToken) {
        CloseHandle(hVSToken);
        hVSToken = NULL;
    }


    pszDriveLetter = new WCHAR[cchDriveLetter];

    if (pszDriveLetter == NULL) {
        TSDISErrorOut(L"Failed to allocate memory for drive letter.\n");
        goto HandleError;
    }

    // Get the drive we're supposed to use.
    dwError = ResUtilFindDependentDiskResourceDriveLetter(hclus, hrSD,
                                                          pszDriveLetter, &cchDriveLetter);

    if (dwError == ERROR_MORE_DATA) {
        // Wow, big drive letter!
        delete [] pszDriveLetter;
        pszDriveLetter = new WCHAR[cchDriveLetter];

        if (pszDriveLetter == NULL) {
            TSDISErrorOut(L"Failed to allocate memory for drive letter\n");
            goto HandleError;
        }

        dwError = ResUtilFindDependentDiskResourceDriveLetter(hclus, hrSD,
                                                              pszDriveLetter, &cchDriveLetter);
    }

    if (dwError != ERROR_SUCCESS) {
        TSDISErrorOut(L"Could not determine resource drive letter.\n");
        delete [] pszDriveLetter;
        pszDriveLetter = NULL;
        goto HandleError;
    }

    // Switch the working directory to that drive.
    if (SetCurrentDirectory(pszDriveLetter) == FALSE) {
        TSDISErrorOut(L"Could not set current directory to that of "
                      L"shared disk %s.  Error=%d\n", pszDriveLetter, 
                      GetLastError());
        delete [] pszDriveLetter;
        pszDriveLetter = NULL;
        goto HandleError;
    }
    fRet = TRUE;

HandleError:

    if (pszDriveLetter != NULL) {
        delete [] pszDriveLetter;
        pszDriveLetter = NULL;
    }
    if (pPropertyList != NULL) {
        LocalFree(pPropertyList);
        pPropertyList = NULL;
    }
    if (hrSD != NULL) {
        CloseClusterResource(hrSD);
        hrSD = NULL;
    }  
    if (hrNetworkName != NULL) {
        CloseClusterResource(hrNetworkName);
        hrNetworkName = NULL;
    }
    if (hclus != NULL) {
        CloseCluster(hclus);
        hclus = NULL;
    }
    return fRet;
}


HRESULT DISJetInitialize()
{
    JET_SESID sesid = JET_sesidNil;;
    JET_TABLEID sessdirtableid;
    JET_TABLEID servdirtableid;
    JET_TABLEID clusdirtableid;
    JET_DBID dbid = JET_dbidNil;

    JET_ERR err = JET_errSuccess;
    JET_TABLECREATE tSess;
    JET_COLUMNCREATE cSess[NUM_SESSDIRCOLUMNS];
    JET_TABLECREATE tServ;
    JET_COLUMNCREATE cServ[NUM_SERVDIRCOLUMNS];
    JET_TABLECREATE tClus;
    JET_COLUMNCREATE cClus[NUM_CLUSDIRCOLUMNS];
    unsigned count;
    DWORD dwError;
    BOOL br;
    SECURITY_ATTRIBUTES SA;
    SYSTEMTIME SystemTime;
    ULARGE_INTEGER ulCurrentTime;
    ULARGE_INTEGER ulLastTime;


    g_dwClusterState = ClusterStateNotInstalled;

    //
    // This is a string security descriptor.  Look up "Security Descriptor 
    // Definition Language" in MSDN for more details.
    //
    // This one says:
    //
    // D: <we are creating a DACL>
    // (A; <Allow ACE>
    // OICI; <Perform object and container inheritance, i.e., let files and 
    //        directories under this one have these attributes>
    // GA <Generic All Access--Full Control>
    // ;;;SY) <SYSTEM>
    // (A;OICI;GA;;;BA) <same for Builtin Administrators group>
    // (A;OICI;GA;;;CO) <same for creator/owner>
    //
    // We'll use it below to create our directory with the right permissions.

    WCHAR *pwszSD = L"D:(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GA;;;CO)";

    // Failover support--before reactivating, check logic versus reading curr directory from registry.


    // First, determine whether we are running in a cluster.  If so, files
    // will have to go on the shared drive.  If not, files will go in
    // JETDISDBDIRECTORYW.
    dwError = GetNodeClusterState(NULL, &g_dwClusterState);

    if (dwError != ERROR_SUCCESS) {
        g_dwClusterState = ClusterStateNotInstalled;
        TSDISErrorOut(L"TSDIS: Unable to get cluster state, err = %d\n", 
                      dwError);
    }

    // Do initialization if running on fail-over cluster
    if (g_dwClusterState == ClusterStateRunning) {
        if (!DISJetInitInCluster()) {
            goto HandleError;
        }
    }


    // Create security descriptor for database directory

    SA.nLength = sizeof(SECURITY_ATTRIBUTES);
    SA.bInheritHandle = FALSE;
    SA.lpSecurityDescriptor = NULL;
    br = ConvertStringSecurityDescriptorToSecurityDescriptor(pwszSD, 
                                                             SDDL_REVISION_1, &(SA.lpSecurityDescriptor), NULL);

    if (br == 0) {
        PostSessDirErrorValueEvent(EVENT_COULDNOTSECUREDIR, GetLastError(), EVENTLOG_ERROR_TYPE);
        goto HandleError;
    }

    // Create the system32\tssesdir directory.
    if (CreateDirectory(JETDISDBDIRECTORYW, &SA) == 0) {
        if (ERROR_ALREADY_EXISTS != (dwError = GetLastError())) {
            PostSessDirErrorValueEvent(EVENT_COULDNOTCREATEDIR, dwError, EVENTLOG_ERROR_TYPE);
            goto HandleError;
        }
    } else {
        // We created it successfully, so set the directory attributes to not 
        // compress.

        // Obtain a handle to the directory.
        HANDLE hSDDirectory = CreateFile(JETDISDBDIRECTORYW, GENERIC_READ | 
                                         GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 
                                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

        if (INVALID_HANDLE_VALUE != hSDDirectory) {
            // We've succeeded opening the directory.

            USHORT CompressionState = COMPRESSION_FORMAT_NONE;
            USHORT OldCompressionState;
            DWORD BytesReturned = 0;

            // Get the current compression state.
            if (DeviceIoControl(hSDDirectory, FSCTL_GET_COMPRESSION,
                                NULL, 0, &OldCompressionState, sizeof(USHORT), 
                                &BytesReturned, NULL) != 0) {

                // If the current compression state is compressed, uncompress.
                if (OldCompressionState != COMPRESSION_FORMAT_NONE) {
                    if (DeviceIoControl(hSDDirectory, FSCTL_SET_COMPRESSION, 
                                        &CompressionState, sizeof(USHORT), NULL, 0, 
                                        &BytesReturned, NULL) == 0) {
                        // Set compression state failed--this should only be a trace,
                        // it may merely mean that the drive is FAT.
                        TSDISErrorOut(L"TSDIS: Set compression state off failed, "
                                      L"lasterr=0x%X\n", GetLastError());
                    } else {
                        PostSessDirErrorValueEvent(EVENT_UNDID_COMPRESSION, 0, EVENTLOG_ERROR_TYPE);
                    }
                }
            }

            CloseHandle(hSDDirectory);

        } else {
            // Nonfatal to have an error opening the directory
            TSDISErrorOut(L"TSDIS: Open directory to change compression state "
                          L"failed, lasterr=0x%X\n", GetLastError());
        }
    }

    // Open the timestamp file, compare with current time
    //  If the time difference is less than a limit, reuse the db
    //  otherwise delete db files
    g_hTimeFile = CreateFile(JETTIMESTAMPFILEW,
                             GENERIC_WRITE,
                             FILE_SHARE_WRITE,
                             &SA,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
    if (g_hTimeFile == INVALID_HANDLE_VALUE) {
        // This file doesn't exist, create new one
        g_hTimeFile = CreateFile(JETTIMESTAMPFILEW,
                                 GENERIC_WRITE,
                                 FILE_SHARE_WRITE,
                                 &SA,
                                 CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL,
                                 NULL);           
    } 
    else {
        if (GetFileTime(g_hTimeFile, NULL, NULL, (FILETIME *)&ulLastTime)) {
            GetSystemTime(&SystemTime);
            if (SystemTimeToFileTime(&SystemTime, (FILETIME *) &ulCurrentTime)) {
                if ((ulCurrentTime.QuadPart - ulLastTime.QuadPart) < g_TimeLimitToDeleteDB) {
                    if (g_dwClusterState == ClusterStateRunning) {
                        g_RepopulateSession = FALSE;
                        TSDISErrorOut(L"SD in restarted within a time limit, database can be reused\n");
                    }
                }
            }
        } 
        else
            TSDISErrorOut(L"SD is not restarted within a time limit, need to delete DB files \n");
    }

    // Recover Servers in Jet Database 
    if (g_RecoverWhenStart > NO_RECOVER_WHEN_START)
        DISJetRecover();
    // Delete database files if tssdis is not running on failover cluster
    //if (g_dwClusterState != ClusterStateRunning)
    if (g_RepopulateSession == TRUE)
        DeleteJetFiles();

    CALL(JetSetSystemParameter(&g_instance, 0, JET_paramSystemPath, 
                               0, JETDISDBDIRECTORY));
    CALL(JetSetSystemParameter(&g_instance, 0, JET_paramTempPath,
                               0, JETDISDBDIRECTORY));
    //CALL(JetSetSystemParameter(&g_instance, 0, JET_paramMaxSessions,
    //        JETDISMAXSESSIONS, NULL));
    CALL(JetSetSystemParameter(&g_instance, 0, JET_paramLogFilePath,
                               0, JETDISDBDIRECTORY));
    CALL(JetSetSystemParameter(&g_instance, 0, JET_paramCircularLog,
                               1, NULL));

    CALL(JetInit(&g_instance));
    CALL(JetBeginSession(g_instance, &sesid, "user", ""));
    err = JetCreateDatabase(sesid, JETDBFILENAME, "", &dbid, 0);

    if (JET_errDatabaseDuplicate == err) {
        JET_COLUMNDEF jcd;

        err = JetAttachDatabase(sesid, JETDBFILENAME, 0);

        // if we get a wrnDatabaseAttached, then we have recovered.  Otherwise,
        // check the return value as usual.
        if (JET_wrnDatabaseAttached != err) {
            CALL(err);
        }

        // Populate our columnid arrays
        CALL(JetOpenDatabase(sesid, JETDBFILENAME, "", &dbid, 0));

        CALL(JetOpenTable(sesid, dbid, "SessionDirectory", NULL, 0, 0, 
                          &sessdirtableid));
        CALL(JetOpenTable(sesid, dbid, "ServerDirectory", NULL, 0, 0, 
                          &servdirtableid));
        CALL(JetOpenTable(sesid, dbid, "ClusterDirectory", NULL, 0, 0, 
                          &clusdirtableid));

        CALL(JetBeginTransaction(sesid));

        for (count = 0; count < NUM_SESSDIRCOLUMNS; count++) {
            CALL(JetGetColumnInfo(sesid, dbid, "SessionDirectory", 
                                  SessionDirectoryColumns[count].szColumnName, &jcd, 
                                  sizeof(jcd), 0));
            sesdircolumnid[count] = jcd.columnid;
        }
        for (count = 0; count < NUM_SERVDIRCOLUMNS; count++) {
            CALL(JetGetColumnInfo(sesid, dbid, "ServerDirectory",
                                  ServerDirectoryColumns[count].szColumnName, &jcd,
                                  sizeof(jcd), 0));
            servdircolumnid[count] = jcd.columnid;
        }
        for (count = 0; count < NUM_CLUSDIRCOLUMNS; count++) {
            CALL(JetGetColumnInfo(sesid, dbid, "ClusterDirectory",
                                  ClusterDirectoryColumns[count].szColumnName, &jcd,
                                  sizeof(jcd), 0));
            clusdircolumnid[count] = jcd.columnid;
        }

        CALL(JetCommitTransaction(sesid, 0));

        goto NormalExit;
    } else {
        CALL(err);
    }

    CALL(JetBeginTransaction(sesid));

    // Set up to create session directory schema
    tSess.cbStruct = sizeof(tSess);
    tSess.szTableName = "SessionDirectory";
    tSess.szTemplateTableName = NULL;
    tSess.ulPages = 0;
    tSess.ulDensity = 100;
    tSess.rgcolumncreate = &cSess[0];
    tSess.cColumns = NUM_SESSDIRCOLUMNS;
    tSess.rgindexcreate = NULL;
    tSess.cIndexes = 0;
    tSess.grbit = JET_bitTableCreateFixedDDL;

    for (count = 0; count < NUM_SESSDIRCOLUMNS; count++) {
        cSess[count].cbStruct = sizeof(JET_COLUMNCREATE);
        cSess[count].szColumnName = SessionDirectoryColumns[count].szColumnName;
        cSess[count].coltyp = SessionDirectoryColumns[count].coltyp;
        cSess[count].cbMax = SessionDirectoryColumns[count].colMaxLen;
        cSess[count].grbit = 0;
        cSess[count].pvDefault = NULL;
        cSess[count].cbDefault = 0;
        cSess[count].cp = 1200;
        cSess[count].columnid = 0;
        cSess[count].err = JET_errSuccess;
    }


    // Actually create the session directory table.
    CALL(JetCreateTableColumnIndex(sesid, dbid, &tSess));

    // Store columnids, tableid for later reference.
    for (count = 0; count < NUM_SESSDIRCOLUMNS; count++) {
        sesdircolumnid[count] = cSess[count].columnid;
    }
    sessdirtableid = tSess.tableid;

    // Create server, session index.
    CALL(JetCreateIndex(sesid, sessdirtableid, "primaryIndex", 0, 
                        "+ServerID\0+SessionID\0", sizeof("+ServerID\0+SessionID\0"), 
                        100));
    // Create index by server for deletion.
    CALL(JetCreateIndex(sesid, sessdirtableid, "ServerIndex", 0,
                        "+ServerID\0", sizeof("+ServerID\0"), 100));
    // Create index for disconnected session retrieval.
    CALL(JetCreateIndex(sesid, sessdirtableid, "DiscSessionIndex", 0,
                        "+UserName\0+Domain\0+State\0", 
                        sizeof("+UserName\0+Domain\0+State\0"), 100));
    // Create index for all session retrieval.
    CALL(JetCreateIndex(sesid, sessdirtableid, "AllSessionIndex", 0,
                        "+UserName\0+Domain\0",
                        sizeof("+UserName\0+Domain\0"), 100));

    // Create server directory.
    tServ.cbStruct = sizeof(tServ);
    tServ.szTableName = "ServerDirectory";
    tServ.szTemplateTableName = NULL;
    tServ.ulPages = 0;
    tServ.ulDensity = 100;
    tServ.rgcolumncreate = &cServ[0];
    tServ.cColumns = NUM_SERVDIRCOLUMNS;
    tServ.rgindexcreate = NULL;
    tServ.cIndexes = 0;
    tServ.grbit = JET_bitTableCreateFixedDDL;

    for (count = 0; count < NUM_SERVDIRCOLUMNS; count++) {
        cServ[count].cbStruct = sizeof(JET_COLUMNCREATE);
        cServ[count].szColumnName = ServerDirectoryColumns[count].szColumnName;
        cServ[count].coltyp = ServerDirectoryColumns[count].coltyp;
        cServ[count].cbMax = ServerDirectoryColumns[count].colMaxLen;
        cServ[count].grbit = 0;
        cServ[count].pvDefault = NULL;
        cServ[count].cbDefault = 0;
        cServ[count].cp = 1200;
        cServ[count].columnid = 0;
        cServ[count].err = JET_errSuccess;
    }
    // Set the autoincrement column to autoincrement
    cServ[0].grbit |= JET_bitColumnAutoincrement;

    CALL(JetCreateTableColumnIndex(sesid, dbid, &tServ));

    for (count = 0; count < NUM_SERVDIRCOLUMNS; count++) {
        servdircolumnid[count] = cServ[count].columnid;
    }
    servdirtableid = tServ.tableid;

    // Create Server Name (IP) index.
    CALL(JetCreateIndex(sesid, servdirtableid, "ServNameIndex", 0,
                        "+ServerAddress\0", sizeof("+ServerAddress\0"), 100));
    // Create Server DNS host Name index.
    CALL(JetCreateIndex(sesid, servdirtableid, "ServDNSNameIndex", 0,
                        "+ServerDNSName\0", sizeof("+ServerDNSName\0"), 100));
    // Create Server ID index.
    CALL(JetCreateIndex(sesid, servdirtableid, "ServerIDIndex", 0,
                        "+ServerID\0", sizeof("+ServerID\0"), 100));
    // Create Pending Reconnect index.
    CALL(JetCreateIndex(sesid, servdirtableid, "ServerAlmostInTimes", 0,
                        "+AlmostInTimeLow\0+AlmostInTimeHigh\0", 
                        sizeof("+AlmostInTimeLow\0+AlmostInTimeHigh\0"), 100));
    // Create the single session index.
    CALL(JetCreateIndex(sesid, servdirtableid, "SingleSessionIndex", 0,
                        "+ClusterID\0+SingleSessionMode\0", 
                        sizeof("+ClusterID\0+SingleSessionMode\0"), 100));
    // Create the ClusterID index.
    CALL(JetCreateIndex(sesid, servdirtableid, "ClusterIDIndex", 0,
                        "+ClusterID\0", sizeof("+ClusterID\0"), 100));

    // Create cluster directory.
    tClus.cbStruct = sizeof(tClus);
    tClus.szTableName = "ClusterDirectory";
    tClus.szTemplateTableName = NULL;
    tClus.ulPages = 0;
    tClus.ulDensity = 100;
    tClus.rgcolumncreate = &cClus[0];
    tClus.cColumns = NUM_CLUSDIRCOLUMNS;
    tClus.rgindexcreate = NULL;
    tClus.cIndexes = 0;
    tClus.grbit = JET_bitTableCreateFixedDDL;

    for (count = 0; count < NUM_CLUSDIRCOLUMNS; count++) {
        cClus[count].cbStruct = sizeof(JET_COLUMNCREATE);
        cClus[count].szColumnName = ClusterDirectoryColumns[count].szColumnName;
        cClus[count].coltyp = ClusterDirectoryColumns[count].coltyp;
        cClus[count].cbMax = ClusterDirectoryColumns[count].colMaxLen;
        cClus[count].grbit = 0;
        cClus[count].pvDefault = NULL;
        cClus[count].cbDefault = 0;
        cClus[count].cp = 1200;
        cClus[count].columnid = 0;
        cClus[count].err = JET_errSuccess;
    }
    // Set the autoincrement column to autoincrement
    cClus[0].grbit |= JET_bitColumnAutoincrement;

    CALL(JetCreateTableColumnIndex(sesid, dbid, &tClus));

    for (count = 0; count < NUM_CLUSDIRCOLUMNS; count++) {
        clusdircolumnid[count] = cClus[count].columnid;
    }
    clusdirtableid = tClus.tableid;

    // Create Cluster Name index.
    CALL(JetCreateIndex(sesid, clusdirtableid, "ClusNameIndex", 
                        JET_bitIndexUnique, "+ClusterName\0", sizeof("+ClusterName\0"), 
                        100));
    // Create cluster ID index.
    CALL(JetCreateIndex(sesid, clusdirtableid, "ClusIDIndex", 0,
                        "+ClusterID\0", sizeof("+ClusterID\0"), 100));


    CALL(JetCommitTransaction(sesid, 0));

    // Tables were opened with exclusive access from CreateTableColumnIndex.
    // Close them now.
NormalExit:
    CALL(JetCloseTable(sesid, sessdirtableid));
    CALL(JetCloseTable(sesid, servdirtableid));
    CALL(JetCloseTable(sesid, clusdirtableid));

    CALL(JetCloseDatabase(sesid, dbid, 0));

    CALL(JetEndSession(sesid, 0));

    LocalFree(SA.lpSecurityDescriptor);
    SA.lpSecurityDescriptor = NULL;

#ifdef DBG
    OutputAllTables();
#endif // DBG

    return 0;

HandleError:
    if (sesid != JET_sesidNil) {
        // Can't really recover.  Just bail out.
        (VOID) JetRollback(sesid, JET_bitRollbackAll);

        // Force the session closed
        (VOID) JetEndSession(sesid, JET_bitForceSessionClosed);
    }

    if (SA.lpSecurityDescriptor != NULL) {
        LocalFree(SA.lpSecurityDescriptor);
        SA.lpSecurityDescriptor = NULL;
    }                                             

    PostSessDirErrorValueEvent(EVENT_JET_COULDNT_INIT, err, EVENTLOG_ERROR_TYPE);

    exit(1);
}


/****************************************************************************/
// DISCleanupGlobals
//
// Common cleanup code for SQL and Jet code paths.
/****************************************************************************/
void DISCleanupGlobals()
{
    if (g_hStopServiceEvent != NULL) {
        CloseHandle(g_hStopServiceEvent);
        g_hStopServiceEvent = NULL;
    }

    if (g_hFileOutput != INVALID_HANDLE_VALUE) {
        if (CloseHandle(g_hFileOutput) == 0) {
            ERR((TB, "CloseHandle on output file failed: lasterr=0x%X", 
                    GetLastError()));
        }
        g_hFileOutput = INVALID_HANDLE_VALUE;
    }

    if (g_hTimeFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hTimeFile);
        g_hTimeFile =  INVALID_HANDLE_VALUE;
    }

    if (g_ClusterNetworkName != NULL) {
        LocalFree(g_ClusterNetworkName);
        g_ClusterNetworkName = NULL;
    }

    if (g_hClusterToken) {
        CloseHandle(g_hClusterToken);
        g_hClusterToken = NULL;
    }
}


#if 0
/****************************************************************************/
// DISCallSPForServer
//
// Generic function to call a stored procedure that takes a ServerAddress as an
// argument.
/****************************************************************************/
void DISCallSPForServer(WCHAR *StoredProcName, WCHAR *ServerAddress) {
    HRESULT hr;
    ADOCommand *pCommand;
    ADOParameters *pParameters;
    ADORecordset *pResultRecordSet;


    hr = CreateADOStoredProcCommand(StoredProcName, &pCommand, &pParameters);

    if (SUCCEEDED(hr)) {
        hr = AddADOInputStringParam(ServerAddress, L"ServerAddress", 
                pCommand, pParameters, FALSE);
        if (SUCCEEDED(hr)) {
            // Execute the command.
            hr = pCommand->Execute(NULL, NULL, adCmdStoredProc, 
                    &pResultRecordSet);
            if (SUCCEEDED(hr)) {
                pResultRecordSet->Release();
            } else {
                ERR((TB, "DISCallSPForServer: Failed Execute, hr = 0x%X", 
                        hr));
            }
        }
        else {
            ERR((TB,"DISCallSPForServer: Failed add parameter, hr=0x%X", hr));
        }

        pParameters->Release();
        pCommand->Release();
    }
    else {
        ERR((TB,"DISCallSPForServer: Failed create cmd, hr=0x%X", hr));
    }
}
#endif


/****************************************************************************/
// DISJetHandleDeadServer
//
// When a server is not responding, this function call sends the command to the
// Jet database to remove all entries pertaining to that server.
/****************************************************************************/
void DISJetHandleDeadServer(WCHAR *ServerAddress, DWORD ServerID) {
    // FailureCount is initially set to 1, TRUE, to tell SetServerAITInternal
    // to increment the failure count and return the resultant count.
    DWORD FailureCount = 1;

    TSSDSetServerAITInternal(ServerAddress, FALSE, &FailureCount);

    TSDISErrorOut(L"Server %s (%d) not responding (Failure Count: %d).\n",
            ServerAddress, ServerID, FailureCount);

    if (FailureCount >= g_NumberFailedPingsBeforePurge)
        TSSDPurgeServer(ServerID);
}


// TODO: Possible optimization: pass in ServerID
void DISJetSetServerPingSuccessful(WCHAR *ServerAddress) {
    TSSDSetServerAITInternal(ServerAddress, TRUE, NULL);
}


#if 0
/****************************************************************************/
// DISSQLHandleDeadServer
//
// When a server is not responding, this function call sends the command to the
// database to execute SP_TSDISServerNotResponding.
/****************************************************************************/
void DISSQLHandleDeadServer(WCHAR *ServerAddress) {
    DISCallSPForServer(L"SP_TSDISServerNotResponding", ServerAddress);
}


void DISSQLSetServerPingSuccessful(WCHAR *ServerAddress) {
    DISCallSPForServer(L"SP_TSDISSetServerPingSuccessful", ServerAddress);
}
#endif


VOID DISCtrlHandler(DWORD opcode) {

    switch(opcode)
    {
    //case SERVICE_CONTROL_PAUSE:
        // pause
    //    g_DISStatus.dwCurrentState = SERVICE_PAUSED;
    //    break;

    //case SERVICE_CONTROL_CONTINUE:
        // continue
    //    g_DISStatus.dwCurrentState = SERVICE_RUNNING;
    //    break;

    case SERVICE_CONTROL_STOP:
        //stop
        g_DISStatus.dwWin32ExitCode = 0;
        g_DISStatus.dwCurrentState = SERVICE_STOP_PENDING;
        g_DISStatus.dwCheckPoint = 0;
        g_DISStatus.dwWaitHint = 0;

        if (!SetServiceStatus(g_DISStatusHandle, &g_DISStatus)) {
            ERR((TB, "SetServiceStatus failed"));
        }

        // Here is where to actually stop the service
        SetEvent(g_hStopServiceEvent);
        // Should I wait for that to complete?

        return;

    case SERVICE_CONTROL_INTERROGATE:
        // fall through to return current status
        break;

    default:
        ERR((TB, "Unrecognized opcode to DISCtrlHandler - 0x%08x", opcode));
    }

    // send current status
    if (!SetServiceStatus(g_DISStatusHandle, &g_DISStatus)) {
        ERR((TB, "SetServiceStatus failed"));
    }
}


void DISDirectoryIntegrityLoop() {
    CVar varRows;
    WCHAR *ServerAddress;
#if 0
    WCHAR ServerAddressBuf[SERVER_ADDRESS_LENGTH];
#endif
    WCHAR ServerAddressRows[10][SERVER_ADDRESS_LENGTH];
    DWORD ServerIDs[10];
    long NumSessionsReturned = 0;
#if 0
    HRESULT hr = S_OK;
#endif
    SERVER_STATUS ServerStatus;
    DWORD EventStatus;

#if 0
    ServerAddress = ServerAddressBuf; // In SQL case, we need a static buffer
#endif

#if 0
    TSDISErrorOut(L"%s active\n", g_bUseSQL ? L"Directory Integrity Service" : 
            L"Session Directory");
#endif

    TSDISErrorOut(L"Session Directory Active\n");
            
    // Loop forever
    for ( ; ; ) {
        // Retrieve set of servers that have disconnected sessions pending
        // reconnects
#if 0
        if (g_bUseSQL == FALSE)
#endif
            DISJetGetServersPendingReconnects(&NumSessionsReturned,
                    ServerAddressRows, ServerIDs);
#if 0
        else
            DISSQLGetServersPendingReconnects(&NumSessionsReturned, 
                    &varRows);
#endif

        // For each server,
        for (DWORD i = 0; i < (unsigned)NumSessionsReturned; i++) {
#if 0
            if (g_bUseSQL == FALSE)
#endif

            ServerAddress = ServerAddressRows[i];

#if 0
            else
                hr = GetRowArrayStringField(varRows.parray, i, 0,
                        ServerAddress, sizeof(ServerAddressBuf) /
                        sizeof(WCHAR) - 1);

            if (FAILED(hr)) {
                ERR((TB,"DISDirectoryIntegrityLoop: Row %u returned hr=0x%X",
                        i, hr));
            }
#endif

            ServerStatus = DISGetServerStatus(ServerAddress);

            // if the server does not respond, handle dead server.
            // The function we call will do the right thing, which may be
            // to purge immediately, or may be to simply increment a failure
            // count.
            if (ServerStatus == NotResponding) {
#if 0
                if (FALSE == g_bUseSQL)
#endif
                DISJetHandleDeadServer(ServerAddress, ServerIDs[i]);
#if 0
                else
                    DISSQLHandleDeadServer(ServerAddress);
#endif

#ifdef DBG
                OutputAllTables();
#endif // DBG
            } 
            // else stop pinging
            else if (ServerStatus == Responding) {
#if 0
                if (FALSE == g_bUseSQL)
#endif
                    DISJetSetServerPingSuccessful(ServerAddress);
#if 0
                else
                    DISSQLSetServerPingSuccessful(ServerAddress);
#endif
            }
            else {
                ERR((TB, "DISDirectoryIntegrityLoop: ServerStatus enum has bad "
                        "value %d", ServerStatus));
            }
        }
        // Wait DISNumberSecondsBetweenPings
        EventStatus = WaitForSingleObjectEx(g_hStopServiceEvent, 
                DISNumberSecondsBetweenPings * 1000, FALSE);
        if (EventStatus == WAIT_TIMEOUT) {
            // do normal stuff
            continue;
        } else if (EventStatus == WAIT_OBJECT_0) {
            // the event was signaled -- clean up
            DISDeleteLocalGroupSecDes();
            break;
        } else if (EventStatus == -1) {
            // there is an error
        } else {
            // weird output from that function
        } 
    }
}
    

#if 0
/****************************************************************************/
// DISSQLStart
//
// Service main entry point for when the service is configured to verify
// SQL tables.
/****************************************************************************/
VOID DISSQLStart(DWORD argc, LPTSTR *argv) {
    HRESULT hr = S_OK;

    // unreferenced parameters
    argv;
    argc;
    
    g_DISStatus.dwServiceType = SERVICE_WIN32;
    g_DISStatus.dwCurrentState = SERVICE_START_PENDING;
    g_DISStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_DISStatus.dwWin32ExitCode = 0;
    g_DISStatus.dwServiceSpecificExitCode = 0;
    g_DISStatus.dwCheckPoint = 0;
    g_DISStatus.dwWaitHint = 0;
    g_DISStatusHandle = RegisterServiceCtrlHandler(
            _T("Directory Integrity Service"), DISCtrlHandler);
    if (g_DISStatusHandle == (SERVICE_STATUS_HANDLE)0) {
        ERR((TB, "DISSQLStart: RegisterServiceCtrlHandler failed"));
        goto ExitFunc;
    }

    // Initialization code goes here
    hr = DISSQLInitialize();
    if (FAILED(hr)) {
        ERR((TB, "DISSQLStart: DISSQLInitialize failed"));
        goto PostRegisterService;
    }

    g_DISStatus.dwCurrentState = SERVICE_RUNNING;
    g_DISStatus.dwCheckPoint = 1;
    if (!SetServiceStatus(g_DISStatusHandle, &g_DISStatus)) {
        ERR((TB, "DISSQLStart: SetServiceHandler failed"));
        goto PostRegisterService;
    }

    DISDirectoryIntegrityLoop();

PostRegisterService:
    g_DISStatus.dwCurrentState = SERVICE_STOPPED;
    g_DISStatus.dwCheckPoint = 2;
    SetServiceStatus(g_DISStatusHandle, &g_DISStatus);

ExitFunc:
    DISCleanupGlobals();
}
#endif


BOOL DISGetSDAdminSid()
{
    DWORD cbSid = 0;
    DWORD dwErr;
    BOOL rc = FALSE;

    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, NULL, &cbSid)) {
        dwErr = GetLastError();
        if (dwErr == ERROR_INSUFFICIENT_BUFFER) {
            g_pAdminSid = LocalAlloc(LMEM_FIXED, cbSid);
        }
        else {
            TSDISErrorOut(L"DISGetSDAdminSid: CreateWellKnownSid fails with %u\n", GetLastError());
            goto HandleError;
        }
    }
    else {
        goto HandleError;
    }

    if (NULL == g_pAdminSid) {
        TSDISErrorOut(L"DISGetSDAdminSid: Memory allocation fails with %u\n", GetLastError());
        goto HandleError;
    }

	if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, g_pAdminSid, &cbSid)) {
        g_pAdminSid = NULL;
        TSDISErrorOut(L"DISGetSDAdminSid: CreateWellKnownSid fails with %u\n", GetLastError());
        goto HandleError;
    }
    rc = TRUE;

HandleError:
    return rc;
}


/****************************************************************************/
// DISCreateLocalGroupSecDes
//
// Create Session Directory Computers local group if not exist
// and create the security descriptor of this local group
/****************************************************************************/
BOOL DISCreateLocalGroupSecDes()
{
    DWORD Error;
    ULONG SidSize, ReferencedDomainNameSize;
    LPWSTR ReferencedDomainName = NULL;
    SID_NAME_USE SidNameUse;
    WCHAR SDLocalGroupName[SDLOCALGROUPNAMELENGTH];
    WCHAR SDLocalGroupDes[SDLOCALGROUPDESLENGTH];
    GROUP_INFO_1 SDGroupInfo = {SDLocalGroupName, SDLocalGroupDes};
    HMODULE HModule = NULL;
    LPBYTE pbBuffer = NULL;
    DWORD dwEntriesRead = 0, dwTotalEntry = 0;
    DWORD_PTR resumehandle = NULL;
   
    NET_API_STATUS NetStatus;
    BOOL rc = FALSE;

    HModule = GetModuleHandle(NULL);
    if (HModule == NULL) {
        Error = GetLastError();
        TSDISErrorOut(L"GetModuleHandle returns error : %u\n", Error);
        goto HandleError;
    }
    if (!LoadString(HModule, IDS_SDLOCALGROUP_NAME, SDLocalGroupName, sizeof(SDLocalGroupName) / sizeof(WCHAR)) ||
        !LoadString(HModule, IDS_SDLOCALGROUP_DES, SDLocalGroupDes, sizeof(SDLocalGroupDes) / sizeof(WCHAR)))
    {
        TSDISErrorOut(L"LoadString fails with %u\n", GetLastError());
        goto HandleError;
    }
    // Create local group if not exist
    NetStatus = NetLocalGroupAdd(
                NULL,
                1,
                (LPBYTE)&SDGroupInfo,
                NULL
                );

    if(NERR_Success != NetStatus) {
        if((NERR_GroupExists != NetStatus)
           && (ERROR_ALIAS_EXISTS != NetStatus)) {
            //
            // Didn't create the group and group doesn't exist either.
            //
            
            TSDISErrorOut(L"NetLocalGroupAdd(%s) returns error: %u\n",
                            SDGroupInfo.grpi1_name, NetStatus);
            goto HandleError;
        }
    }
    
    //
    // Group created. Now lookup the SID.
    //
    SidSize = ReferencedDomainNameSize = 0;
    ReferencedDomainName = NULL;
    NetStatus = LookupAccountName(
                NULL,
                SDGroupInfo.grpi1_name,
                g_pSid,
                &SidSize,
                ReferencedDomainName,
                &ReferencedDomainNameSize,
                &SidNameUse);
    if( NetStatus ) 
        goto HandleError;       
        
    Error = GetLastError();
    if( ERROR_INSUFFICIENT_BUFFER != Error ) 
        goto HandleError;
        
    g_pSid = (PSID)LocalAlloc(LMEM_FIXED, SidSize);
    if (NULL == g_pSid) {
        goto HandleError;
    }
    ReferencedDomainName = (LPWSTR)LocalAlloc(LMEM_FIXED,
                                              sizeof(WCHAR)*(1+ReferencedDomainNameSize));
    if (NULL == ReferencedDomainName) {
        goto HandleError;
    }
        
    NetStatus = LookupAccountName(
                NULL,
                SDGroupInfo.grpi1_name,
                g_pSid,
                &SidSize,
                ReferencedDomainName,
                &ReferencedDomainNameSize,
                &SidNameUse
                );
    if( 0 == NetStatus ) {
        //
        // Failed.
        //
        Error = GetLastError();
        TSDISErrorOut(L"LookupAccountName failed with %u\n", Error);            
        goto HandleError;
    }
        
    // Get the members of the local group
    NetStatus = NetLocalGroupGetMembers(
                    NULL,
                    SDGroupInfo.grpi1_name,
                    0,
                    &pbBuffer,
                    MAX_PREFERRED_LENGTH,
                    &dwEntriesRead,
                    &dwTotalEntry,
                    &resumehandle
                    );
    if (NERR_Success == NetStatus) {
        if (dwEntriesRead == 0) {
            // Th group is emptry, throw the event log
            PostSessDirErrorMsgEvent(EVENT_SD_GROUP_EMPTY, SDGroupInfo.grpi1_name, EVENTLOG_WARNING_TYPE);
        }
        else {
            if (pbBuffer) {
                NetApiBufferFree(pbBuffer);
                pbBuffer = NULL;
            }
        }
    }
    else {
        TSDISErrorOut(L"NetLocalGroupGetMembersfailed with %d\n", NetStatus);
    }

    rc = TRUE;
    return rc;

HandleError:
    if (ReferencedDomainName)
        LocalFree(ReferencedDomainName);
    // Clean
    DISDeleteLocalGroupSecDes();

    return rc;
}

void DISDeleteLocalGroupSecDes()
{
    if (g_pSid) {
        LocalFree(g_pSid);
        g_pSid = NULL;
    }

    if (g_pAdminSid) {
        LocalFree(g_pAdminSid);
        g_pAdminSid = NULL;
    }
}

/****************************************************************************/
// DISJetStart
//
// Service main entry point for when the service is configured to act as
// an RPC server and use Jet for all session directory transactions.
/****************************************************************************/
VOID DISJetStart(DWORD argc, LPTSTR *argv) {
    RPC_STATUS Status;
    RPC_BINDING_VECTOR *pBindingVector = 0;
    RPC_POLICY rpcpol = {sizeof(rpcpol), 0, 0};
    WCHAR *szPrincipalName = NULL;

    // unreferenced parameters
    argv;
    argc;


    g_DISStatus.dwServiceType = SERVICE_WIN32;
    g_DISStatus.dwCurrentState = SERVICE_START_PENDING;
    g_DISStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_DISStatus.dwWin32ExitCode = 0;
    g_DISStatus.dwServiceSpecificExitCode = 0;
    g_DISStatus.dwCheckPoint = 0;
    g_DISStatus.dwWaitHint = 0;

    if (g_bDebug == FALSE) {
        g_DISStatusHandle = RegisterServiceCtrlHandler(
                _T("Directory Integrity Service"), DISCtrlHandler);

        if (g_DISStatusHandle == (SERVICE_STATUS_HANDLE)0) {
            ERR((TB, "DISJetStart: RegisterServiceCtrlHandler failed"));
            goto ExitFunc;
        }
    }

    // Init the RPC server interface.
    // Register the named pipe. This uses NT domain authentication.

    /*
    Status = RpcServerUseProtseqEp(
            L"ncacn_np",  // Protocol Sequence
            NUM_JETRPC_THREADS,  // Maximum calls at one time
            L"\\pipe\\TSSD_Jet_RPC_Service",  // Endpoint
            NULL);  // Security
    */

    if (!DISCreateLocalGroupSecDes()) {
        ERR((TB,"DISJetStart: Error in DISCreateLocalGroupSecDEs"));
        goto PostRegisterService;
    }

    // Get the Sid of the Admin of SD machine
    DISGetSDAdminSid();

    Status = RpcServerUseProtseqEx(L"ncacn_ip_tcp", 3, 0, &rpcpol);
    if (Status != RPC_S_OK) {
        ERR((TB,"DISJetStart: Error %d RpcUseProtseqEp on ncacn_ip_tcp", 
                Status));
        PostSessDirErrorValueEvent(EVENT_FAIL_RPC_INIT_USEPROTSEQ, Status, EVENTLOG_ERROR_TYPE);
        goto PostRegisterService;
    }

    // Register our interface handle (found in jetrpc.h).
    Status = RpcServerRegisterIfEx(TSSDJetRPC_ServerIfHandle, NULL, NULL,
                                    0, RPC_C_LISTEN_MAX_CALLS_DEFAULT, SDRPCAccessCheck);
    if (Status != RPC_S_OK) {
        ERR((TB,"DISJetStart: Error %d RegIf", Status));
        PostSessDirErrorValueEvent(EVENT_FAIL_RPC_INIT_REGISTERIF, Status, EVENTLOG_ERROR_TYPE);
        goto PostRegisterService;
    }   

    Status = RpcServerInqBindings(&pBindingVector);

    if (Status != RPC_S_OK) {
        ERR((TB,"DISJetStart: Error %d InqBindings", Status));
        PostSessDirErrorValueEvent(EVENT_FAIL_RPC_INIT_INQBINDINGS, Status, EVENTLOG_ERROR_TYPE);
        goto PostRegisterService;
    }

    Status = RpcEpRegister(TSSDJetRPC_ServerIfHandle, pBindingVector, 0, 0);
    // TODO: Probably need to unregister, maybe delete some binding vector.

    if (Status != RPC_S_OK) {
        ERR((TB,"DISJetStart: Error %d EpReg", Status));
        PostSessDirErrorValueEvent(EVENT_FAIL_RPC_INIT_EPREGISTER, Status, EVENTLOG_ERROR_TYPE);
        goto PostRegisterService;
    }

    Status = RpcServerInqDefaultPrincName(RPC_C_AUTHN_GSS_NEGOTIATE, &szPrincipalName);
    if (Status != RPC_S_OK) {
        ERR((TB,"DISJetStart: Error %d ServerIngDefaultPrincName", Status));
        PostSessDirErrorValueEvent(EVENT_FAIL_RPC_INIT_INGPRINCNAME, Status, EVENTLOG_ERROR_TYPE);
        goto PostRegisterService;
    }

    Status = RpcServerRegisterAuthInfo(szPrincipalName, RPC_C_AUTHN_GSS_NEGOTIATE, NULL, NULL);
    RpcStringFree(&szPrincipalName);
    if (Status != RPC_S_OK) {
        ERR((TB,"DISJetStart: Error %d ServerRegisterAuthInfo", Status));
        PostSessDirErrorValueEvent(EVENT_FAIL_RPC_INIT_REGAUTHINFO, Status, EVENTLOG_ERROR_TYPE);
        goto PostRegisterService;
    }


    // Now initialize the JET database
    DISJetInitialize();

    // Init the RPC to support the query for SD
    Status = SDInitQueryRPC();
    if (Status != RPC_S_OK) {
        TSDISErrorOut(L"SDInitQueryRPC fails with %d\n", Status);
    }

    // Now do the RPC listen to service calls
    Status = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, TRUE);
    if (Status != RPC_S_OK) {
        ERR((TB,"DISJetStart: Error %d ServerListen", Status));
        PostSessDirErrorValueEvent(EVENT_FAIL_RPC_LISTEN, Status, EVENTLOG_ERROR_TYPE);
        goto PostRegisterService;
    }

    // We are now up.
    g_DISStatus.dwCurrentState = SERVICE_RUNNING;
    g_DISStatus.dwCheckPoint = 1;
    if (g_bDebug == FALSE)
        SetServiceStatus(g_DISStatusHandle, &g_DISStatus);

    // Now we have the RPC server running, we can just wait for the
    // service-stop event to be fired to let us know we need to exit.
    // We do this inside the Directory Integrity Loop.
    DISDirectoryIntegrityLoop();

    // Time to clean up.
    // Kill the RPC listener.
    RpcServerUnregisterIf(TSSDJetRPC_ServerIfHandle, NULL, NULL);
    RpcServerUnregisterIf(TSSDQUERYRPC_ServerIfHandle, NULL, NULL);
    
    TSDISErrorOut(L"Session Directory Stopped\n");

    JetTerm(g_instance);

PostRegisterService:    

    g_DISStatus.dwCurrentState = SERVICE_STOPPED;
    g_DISStatus.dwCheckPoint = 2;
    if (g_bDebug == FALSE) {
        if (!SetServiceStatus(g_DISStatusHandle, &g_DISStatus)) {
            ERR((TB, "SetServiceStatus failed: %d", GetLastError()));
        }
    }

ExitFunc:
    DISCleanupGlobals();
}


/****************************************************************************/
// DISInstallService
//
// Used to install the service, returns 0 on success, nonzero otherwise.
/****************************************************************************/
int DISInstallService() {
    WCHAR wzModulePathname[MAX_PATH];
    SC_HANDLE hSCM = NULL, hService = NULL;

    if (0 != GetModuleFileNameW(NULL, wzModulePathname, MAX_PATH)) {
        hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
        if (hSCM != NULL) {
            hService = CreateServiceW(hSCM, L"Directory Integrity Service",
                    L"Directory Integrity Service", 0, 
                    SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                    SERVICE_ERROR_NORMAL, wzModulePathname, NULL, NULL, NULL,
                    NULL, NULL);
            if (hService != NULL) {
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCM);
            } else {
                ERR((TB, "CreateService failed, error = 0x%X", GetLastError()));
                CloseServiceHandle(hSCM);
                return -1;
            }
        } else {
            ERR((TB, "OpenSCManager failed, error = 0x%X", GetLastError()));
            return -1;
        }
    } else {
        ERR((TB, "GetModuleFileNameW failed, error = 0x%X", GetLastError()));
        return -1;
    }

    return 0;
}


/****************************************************************************/
// DISRemoveService()
//
// Used to remove the service, returns 0 on success, nonzero otherwise.
/****************************************************************************/
int DISRemoveService() {
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);

    if (hSCM != NULL) {
        // Open this service for DELETE access
        SC_HANDLE hService = OpenServiceW(hSCM, L"Directory Integrity Service",
                DELETE);
        if (hService != NULL) {
            // Remove this service from the SCM's database.
            DeleteService(hService);
            CloseServiceHandle(hService);
            CloseServiceHandle(hSCM);

            return 0;
        } else {
            ERR((TB, "Failure opening service for delete, error = 0x%X", 
                    GetLastError()));
        }
        CloseServiceHandle(hService);
    } else {
        ERR((TB, "Failure opening SC Manager, error = 0x%X", GetLastError()));
    }

    return -1;
}


// Reads a DWORD value out of the registry.
//
// In:
//  hKey - an open HKEY
//  RegValName - the name of the registry value
//  pValue - pointer to the value.  The value will be set to the registry value
//    if the registry operation is a success, else it will remain untouched.
//
// Out:
//  0 if success, nonzero otherwise
int ReadRegVal(HKEY hKey, WCHAR *RegValName, DWORD *pValue)
{
    DWORD RegRetVal;
    DWORD Type, Temp, Size;

    Size = sizeof(Temp);
    RegRetVal = RegQueryValueExW(hKey, RegValName, NULL, &Type,
            (BYTE *)&Temp, &Size);
    if (RegRetVal == ERROR_SUCCESS) {
        *pValue = Temp;
        return 0;
    }
    else {
        TRC1((TB, "TSSDIS: Failed RegQuery for %S - "
                "err=%u, DataSize=%u, type=%u\n",
                RegValName, RegRetVal, Size, Type));
        return -1;
    }

}


// Reads a Unicode text value out of the registry.
//
// hKey (IN) - an open HKEY
// RegValName (IN) - the name of the registry value
// pText (IN/OUT) - pointer to the buffer to which to write.
// cbData (IN) - size of buffer IN BYTES
//
// returns 0 if success, nonzero otherwise.
int ReadRegTextVal(HKEY hKey, WCHAR *RegValName, WCHAR *pText, DWORD cbData)
{
    DWORD RegRetVal;
    DWORD Type, Size;

    Size = cbData;

    RegRetVal = RegQueryValueExW(hKey, RegValName, NULL, &Type,
            (BYTE *)pText, &Size);

    if (RegRetVal == ERROR_SUCCESS) {
        return 0;
    }
    else {
        TRC1((TB, "TSSDIS: Failed RegQuery for %S - err=%u, DataSize=%u, "
                "type=%u\n", RegValName, RegRetVal, Size, Type));
        return -1;
    }
}

// Reads configuration from the registry and sets global variables.
void ReadConfigAndSetGlobals()
{
    DWORD RegRetVal;
    HKEY hKey;
    DWORD Temp;
    WCHAR WorkingDirectory[MAX_PATH];
    WCHAR *pwszSD = L"D:(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GA;;;CO)";
    SECURITY_ATTRIBUTES SA;
    BOOL br;

    // Open the service settings regkey and grab the UseJet flag.
    // Absence of the key or the setting means no jet.
#if 0
    g_bUseSQL = FALSE;
#endif
    RegRetVal = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
            REG_SESSION_DIRECTROY_CONTROL, 0, KEY_READ, &hKey);
    if (RegRetVal == ERROR_SUCCESS) {

        // With each of these calls, an error is non-fatal.
#if 0
        // Query UseSQL value.
        ReadRegVal(hKey, L"UseSQL", &g_bUseSQL);
#endif

        // Query PingMode value.  Note this is an enum so sending the variable
        // in directly is illegal.
        if (ReadRegVal(hKey, L"PingMode", &Temp) == 0) {

            // Make sure this is a legal value for the enum.
            if (Temp > AlwaysFail)
                Temp = NormalMode;

            g_PingMode = (PingMode) Temp;
        }

        // Query TraceOutputMode value.  As above, enum means don't set it
        // directly.
        if (ReadRegVal(hKey, L"TraceOutputMode", &Temp) == 0) {

            // Make sure this is a legal value for the enum.
            if (Temp > FileOutput)
                Temp = NoTraceOutput;

            g_TraceOutputMode = (TraceOutputMode) Temp;

        }

        // Query NumberFailedPingsBeforePurge.
        ReadRegVal(hKey, L"NumberFailedPingsBeforePurge", 
                &g_NumberFailedPingsBeforePurge);

        // Query TimeBetweenPings.
        ReadRegVal(hKey, L"TimeBetweenPings", &DISNumberSecondsBetweenPings);

        // Query TimeServerSilentBeforePing.
        if (ReadRegVal(hKey, L"TimeServerSilentBeforePing", &Temp) == 0) {
            g_TimeServerSilentBeforePing = (ULONGLONG) Temp * 
                    FILETIME_INTERVAL_TO_SECONDS_MULTIPLIER;
        }

        // Query Working Directory
        if (ReadRegTextVal(hKey, L"WorkingDirectory", WorkingDirectory, 
                sizeof(WorkingDirectory)) == 0) {
            if (SetCurrentDirectory(WorkingDirectory) == 0) {
                DWORD Err;

                Err = GetLastError();
                PostSessDirErrorValueEvent(EVENT_PROBLEM_SETTING_WORKDIR, Err, EVENTLOG_ERROR_TYPE);
                ERR((TB, "TERMSRV: Unable to set directory to value read from "
                        "registry.  LastErr=0x%X", Err));
            }
        }
        
        // Query if we reover previous jet database when starting SD
        ReadRegVal(hKey, L"RecoverWhenStart", &g_RecoverWhenStart);
        
        RegCloseKey(hKey);

        // Now, if in file output mode, open the file.
        if (g_TraceOutputMode == FileOutput) {
            // Create security descriptor for the log file
            SA.nLength = sizeof(SECURITY_ATTRIBUTES);
            SA.bInheritHandle = FALSE;
            SA.lpSecurityDescriptor = NULL;
            br = ConvertStringSecurityDescriptorToSecurityDescriptor(pwszSD, 
                SDDL_REVISION_1, &(SA.lpSecurityDescriptor), NULL);

            if (br == TRUE) {
                g_hFileOutput = CreateFile(DEBUG_LOG_FILENAME, GENERIC_WRITE,
                        FILE_SHARE_READ, &SA, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                        NULL);

                if (g_hFileOutput == INVALID_HANDLE_VALUE) {
                    ERR((TB, "Could not open debug log file, lasterror=0x%X", 
                            GetLastError()));
                    g_TraceOutputMode = NoTraceOutput;
                } 
                else {
                    DWORD dwRetVal = 0;
                    // Set the insertion point to the end of the file and output 
                    // something.
                    dwRetVal = SetFilePointer(g_hFileOutput, 0, NULL, FILE_END);

                    if (dwRetVal == INVALID_SET_FILE_POINTER) {
                        ERR((TB, "Could not set to end of file, lasterror=0x%X",
                                GetLastError()));
                        g_TraceOutputMode = NoTraceOutput;
                    }
                    else {
                        DWORD dwBytesWritten = 0;
                        char *pszIntro = "\n\nNEW INSTANCE\n";
                    
                        if (WriteFile(g_hFileOutput, pszIntro, 
                                (DWORD) strlen(pszIntro), &dwBytesWritten, 
                                NULL) == 0) {
                            ERR((TB, "WriteFile failed, lasterr=0x%X", 
                                    GetLastError()));
                        }
                    }
                }
            }
            else {
                ERR((TB, "ConvertStringSecurityDescriptorToSecurityDescriptor fails with 0x%X",
                                GetLastError()));
                g_TraceOutputMode = NoTraceOutput;
            }
        }

    }
    else {
        WRN((TB,"TERMSRV: Unable to open settings key in HKLM, "
                "lasterr=0x%X", GetLastError()));
    }
}


/*****************************************************************************
 *  SDInitQueryRPC
 *
 *   Setup the RPC bindings, and listen for incoming requests.
 ****************************************************************************/
RPC_STATUS
SDInitQueryRPC(VOID)
{
    RPC_STATUS Status;

    // register the LPC (local only) interface
    Status = RpcServerUseProtseqEp(
                 L"ncalrpc",      // Protocol Sequence (LPC)
                 NUM_JETRPC_THREADS,  // Maximum calls at one time
                 SD_QUERY_ENDPOINT_NAME,    // Endpoint
                 NULL           // Security
                 );

    if( Status != RPC_S_OK ) {
        ERR((TB,"SDInitQueryRPC: Error %d RpcuseProtseqEp on ncalrpc", Status));
        return( Status );
    }

    Status = RpcServerRegisterIfEx(TSSDQUERYRPC_ServerIfHandle, NULL, NULL,
                                    0, RPC_C_LISTEN_MAX_CALLS_DEFAULT, SDQueryRPCAccessCheck);
    if( Status != RPC_S_OK ) {
        ERR((TB,"SDInitQueryRPC: Error %d RpcServerRegisterIf", Status));
        return( Status );
    }

    return RPC_S_OK;
}


int __cdecl main() {
    int nArgc;
    WCHAR **ppArgv = (WCHAR **) CommandLineToArgvW(GetCommandLineW(), &nArgc);
    BOOL fStartService = (nArgc < 2);
    int i;
    HANDLE hMutex;

    if ((fStartService == FALSE) && (ppArgv == NULL)) {
        PostSessDirErrorValueEvent(EVENT_NO_COMMANDLINE, GetLastError(), EVENTLOG_ERROR_TYPE);
        return -1;
    }
    
    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        { _T("Directory Integrity Service"), DISJetStart },  // Default to the
                                                             // Jet version.
        { NULL, NULL }
    };

    for (i = 1; i < nArgc; i++) {
        if ((ppArgv[i][0] == '-') || (ppArgv[i][0] == '/')) {
            if (wcscmp(&ppArgv[i][1], L"install") == 0) {
                if (DISInstallService()) {
                    ERR((TB, "Could not install service"));
                }
            }
            if (wcscmp(&ppArgv[i][1], L"remove") == 0) {
                if (DISRemoveService()) {
                    ERR((TB, "Could not remove service"));
                }
            }
            if (wcscmp(&ppArgv[i][1], L"debug") == 0) {
                TSDISErrorOut(L"Debugging Jet-based Session Directory\n");

                g_hStopServiceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

                g_bDebug = TRUE;

                // Only allow one session directory at a time.  System will close the
                // handle automatically when the process terminates.
                hMutex = CreateMutex(NULL, FALSE, 
                        _T("Global\\Windows Terminal Server Session Directory"));

                if (hMutex == NULL) {
                    // Handle creation failed, not because it already existed.
                    PostSessDirErrorValueEvent(EVENT_PROBLEM_CREATING_MUTEX, 
                    GetLastError(), EVENTLOG_ERROR_TYPE);
                    return -1;
                }
    
                if (GetLastError() == ERROR_ALREADY_EXISTS) {
                    // Already a session directory out there.
                    PostSessDirErrorValueEvent(EVENT_TWO_SESSDIRS, 0, EVENTLOG_ERROR_TYPE);
                    return -1;
                }


                // Log to stdout by default in this mode, but can be
                // overridden by the registry.
                g_TraceOutputMode = StdOutput;
                
                ReadConfigAndSetGlobals();

                SetConsoleCtrlHandler(DISDebugControlHandler, TRUE);

                DISJetStart(nArgc, ppArgv);
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, (PVOID) ppArgv);

    if (fStartService) {
        // Stop event - signals for the ServiceMain thread to exit.
        g_hStopServiceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

        ReadConfigAndSetGlobals();

#if 0
        if (g_bUseSQL) {
            // Switch from the default to the SQL service start.
            DispatchTable[0].lpServiceProc = DISSQLStart;
        }
#endif

        if (!StartServiceCtrlDispatcher(DispatchTable)) {
#ifdef DBG
            DWORD dw = GetLastError();
#endif // DBG
            ERR((TB, "Could not start service control dispatcher, error 0x%X",
                    dw));
        }
    }

    return 0;
}

#pragma warning (pop)

