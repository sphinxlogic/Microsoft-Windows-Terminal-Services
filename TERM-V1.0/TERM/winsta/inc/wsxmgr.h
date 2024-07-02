
/*************************************************************************
*
* wsxmgr.h
*
* Data to manage Window Station extensions.
*
* Copyright Microsoft Corporation, 1998
*
*  
*************************************************************************/


/*=============================================================================
==   ICA Server supplied procs
=============================================================================*/

/*
 *  Callback workers
 */

typedef VOID (WINAPI * PCALLBACK_PRIMARY)(PVOID, PVOID, PVOID);
typedef VOID (WINAPI * PCALLBACK_COMPLETION)(PVOID);


/*
 *  ICASRV Exported Functions
 */

typedef VOID (WINAPI * PICASRV_NOTIFYSYSTEMEVENT)(ULONG);
typedef VOID (WINAPI * PICASRV_SENDWINSTATIONMESSAGE)(ULONG, PWCHAR, PWCHAR, ULONG);
typedef VOID (WINAPI * PICASRV_GETCONTEXTFORLOGONID)(ULONG, PVOID *);
typedef VOID (WINAPI * PICASRV_WINSTATIONENUMCALLBACK)(PCALLBACK_PRIMARY,
                                                       PCALLBACK_COMPLETION,
                                                       PVOID);

/*
 * Typedefs and structures
 */
typedef struct _ICASRVPROCADDR {

    ULONG                           cbProcAddr;

    PICASRV_NOTIFYSYSTEMEVENT       pNotifySystemEvent;
    PICASRV_SENDWINSTATIONMESSAGE   pSendWinStationMessage;
    PICASRV_GETCONTEXTFORLOGONID    pGetContextForLogonId;
    PICASRV_WINSTATIONENUMCALLBACK  pWinStationEnumCallBack;

} ICASRVPROCADDR, * PICASRVPROCADDR;



/*
 * Exported function prototypes
 */

//  Initialization

typedef BOOL (WINAPI * PWSX_INITIALIZE)(PICASRVPROCADDR);
typedef NTSTATUS (WINAPI * PWSX_WINSTATIONINITIALIZE)(PVOID *);
typedef NTSTATUS (WINAPI * PWSX_WINSTATIONREINITIALIZE)(PVOID, PVOID);
typedef NTSTATUS (WINAPI * PWSX_WINSTATIONRUNDOWN)(PVOID);



//  Client Drive Mapping Extensions

typedef NTSTATUS (WINAPI * PWSX_CDMCONNECT)(PVOID, ULONG, HANDLE);
typedef NTSTATUS (WINAPI * PWSX_CDMDISCONNECT)(PVOID, ULONG, HANDLE);



//  License Extensions

typedef NTSTATUS (WINAPI * PWSX_VERIFYCLIENTLICENSE)(PVOID, SDCLASS);
typedef NTSTATUS (WINAPI * PWSX_GETLICENSE)(PVOID, HANDLE, ULONG, BOOL);
typedef NTSTATUS (WINAPI * PWSX_QUERYLICENSE)(PVOID, ULONG);
typedef DWORD (WINAPI * PWSX_WINSTATIONGENERATELICENSE)(PWCHAR, ULONG, PCHAR, ULONG);
typedef DWORD (WINAPI * PWSX_WINSTATIONINSTALLLICENSE)(PCHAR, ULONG);
typedef DWORD (WINAPI * PWSX_WINSTATIONENUMERATELICENSES)(PULONG, PULONG, PCHAR, PULONG);
typedef DWORD (WINAPI * PWSX_WINSTATIONACTIVATELICENSE)(PCHAR, ULONG, PWCHAR, ULONG);
typedef DWORD (WINAPI * PWSX_WINSTATIONREMOVELICENSE)(PCHAR, ULONG);
typedef DWORD (WINAPI * PWSX_WINSTATIONSETPOOLCOUNT)(PCHAR, ULONG);
typedef DWORD (WINAPI * PWSX_WINSTATIONQUERYUPDATEREQUIRED)(PULONG);
typedef NTSTATUS (WINAPI * PWSX_WINSTATIONLOGONANNOYANCE)(ULONG);
typedef DWORD (WINAPI * PWSX_WINSTATIONANNOYANCETHREAD)(PVOID);


//  Context 


typedef NTSTATUS (WINAPI * PWSX_DUPLICATECONTEXT)(PVOID, PVOID *);
typedef NTSTATUS (WINAPI * PWSX_COPYCONTEXT)(PVOID, PVOID);
typedef NTSTATUS (WINAPI * PWSX_CLEARCONTEXT)(PVOID);


//  Other


typedef NTSTATUS (WINAPI * PWSX_VIRTUALCHANNELSECURITY)(PVOID, HANDLE, PUSERCONFIG);
typedef NTSTATUS (WINAPI * PWSX_ICASTACKIOCONTROL)(PVOID, HANDLE, HANDLE, ULONG, PVOID, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS (WINAPI * PWSX_INITIALIZECLIENTDATA)(PVOID, HANDLE, HANDLE, HANDLE,
                                                      BYTE *, ULONG, PUSERCONFIG, 
                                                      PUSHORT, PUSHORT, PUSHORT,
                                                      WINSTATIONDOCONNECTMSG *);
typedef NTSTATUS (WINAPI * PWSX_INITIALIZEUSERCONFIG)(PVOID, HANDLE, HANDLE, PUSERCONFIG,
                                                      PUSHORT, PUSHORT, PUSHORT);
typedef NTSTATUS (WINAPI * PWSX_CONVERTPUBLISHEDAPP)(PVOID, PUSERCONFIG);
typedef NTSTATUS (WINAPI * PWSX_CHECKFORAPPLICATIONNAME)(PVOID, PWCHAR, ULONG, PWCHAR, ULONG,  
                                                         PWCHAR, PULONG, ULONG, PCHAR, PBOOLEAN, PBOOLEAN ); 
typedef NTSTATUS (WINAPI * PWSX_GETAPPLICATIONINFO)(PVOID, PBOOLEAN, PBOOLEAN); 
typedef NTSTATUS (WINAPI * PWSX_BROKENCONNECTION)(PVOID, HANDLE, PICA_BROKEN_CONNECTION); 
typedef NTSTATUS (WINAPI * PWSX_LOGONNOTIFY)(PVOID, ULONG, HANDLE, PWCHAR, PWCHAR); 
typedef NTSTATUS (WINAPI * PWSX_SETERRORINFO)(PVOID, UINT32, BOOL); 
typedef NTSTATUS (WINAPI * PWSX_SENDAUTORECONNECTSTATUS)(PVOID, UINT32, BOOL); 
// added for long UserName, Password support
typedef NTSTATUS (WINAPI * PWSX_ESCAPE) (PVOID, INFO_TYPE, PVOID, ULONG, PVOID, ULONG, PULONG); 







