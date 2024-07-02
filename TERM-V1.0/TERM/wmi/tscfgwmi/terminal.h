/******************************************************************
   Copyright (C) 2000 Microsoft Corp.

   Terminal.H -- WMI provider class definition

   Generated by Microsoft WBEM Code Generation Engine
 
   Description: 
   

*******************************************************************/

// Property set identification
//============================

#ifndef _TerminalWinstation_H_
#define _TerminalWinstation_H_
#include "resource.h"
#include <winsta.h>
#include <regapi.h>
#include <utilsub.h>
#include <allproc.h>
#include "trace.h"


#define PROVIDER_NAME_Win32_WIN32_TERMINALSERVICESETTING_Prov L"Win32_TerminalServiceSetting"
#define PROVIDER_NAME_Win32_WIN32_TSPROVIDER_Prov L"Win32_TSProvider"
#define PROVIDER_NAME_Win32_WIN32_TERMINALSERVICE_Prov L"Win32_TerminalService"
#define PROVIDER_NAME_Win32_WIN32_TSSESSIONDIRECTORY_Prov L"Win32_TSSessionDirectory"



#define SIZE_OF_BUFFER( x ) sizeof( x ) / sizeof( TCHAR )

extern BOOL g_bInitialized;

// See ExecQuery for details of the usage of these #defines
#define BIT_MODE                            0x00000001
#define BIT_LICENSING                       0x00000002
#define BIT_ACTIVEDESKTOP                   0x00000004
#define BIT_USERPERM                        0x00000008
#define BIT_DELETETEMPDIRS                  0x00000010
#define BIT_PERSESSIONTEMPDIR               0x00000020
#define BIT_LOGONS                          0x00000040
#define BIT_TOTALSESSIONS                   0x00000080
#define BIT_DISCONNECTEDSESSIONS            0x00000100
#define BIT_ESTIMATEDSESSIONCAPACITY        0x00000200
#define BIT_RESOURCECONSTRAINT              0x00000400
#define BIT_RAWSESSIONCAPACITY              0x00000800
#define BIT_SESSIONDIRECTORY                0x00001000
#define BIT_CLUSTERNAME                     0x00002000
#define BIT_HELP                            0x00004000
#define BIT_ADDITIONALPARAMS                0x00008000
#define BIT_SESSIONDIRECTORYACTIVE          0x00010000
#define BIT_ALLOWTSCONNECTIONS              0x00020000
#define BIT_SINGLESESSION                   0x00040000
#define BIT_PROFILEPATH                     0x00080000
#define BIT_HOMEDIRECTORY                   0x00100000
#define BIT_SESSIONDIRECTORYEXPOSESERVERIP  0x00200000
#define BIT_DIRECTCONNECTLICENSESERVERS     0x00400000
#define BIT_SERVERNAME                      0x00800000
#define BIT_TIMEZONEREDIRECTION             0x01000000
#define BIT_SESSIONDIRECTORYIPADDRESS       0x02000000
#define BIT_DISABLEFORCIBLELOGOFF           0x04000000

#define BIT_ALL_PROPERTIES 0xffffffff

#define SESSDIR_LENGTH 64
#define OPAQUESETTINGS_LENGTH 256 

// Property name externs -- defined in Terminal.cpp
//=================================================

//=--------------------------


class CStackClass
{
    public:
        // Constructor/destructor
        //=======================

        CStackClass();
        virtual ~CStackClass();
        IClassFactory *m_pCfg;
        ICfgComp *m_pCfgComp;
		

    protected:
        // Reading Functions
        //============================
        

        // Writing Functions    
        //============================
       

        // TO DO: Declare any additional functions and accessor
        // functions for private data used by this class
        //===========================================================

    

    private:
        // All data members for CTerminalWinstation should be included here.  
		
   
        
} ;

//=----------



class CWin32_TerminalService : public Provider
{
    public:
        // Constructor/destructor
        //=======================

        CWin32_TerminalService( LPCWSTR lpwszName, LPCWSTR lpwszNameSpace );
        virtual ~CWin32_TerminalService();

    protected:
        // Reading Functions
        //============================
        virtual HRESULT EnumerateInstances(MethodContext*  pMethodContext, long lFlags = 0L);
        virtual HRESULT GetObject(CInstance *pInstance, long lFlags, CFrameworkQuery &Query);
        virtual HRESULT ExecQuery(MethodContext *pMethodContext, CFrameworkQuery& Query, long lFlags = 0L);

        // Writing Functions    
        //============================
       

        // TO DO: Declare any additional functions and accessor
        // functions for private data used by this class
        //===========================================================

        HRESULT LoadPropertyValues( CInstance *pInstance, DWORD dwRequiredProperties, WINSTATIONLOADINDICATORDATA LIData);
    

    private:
        // All data members for CTerminalWinstation should be included here.  
        
      
        TCHAR m_szTotalSessions[ 64 ];
        TCHAR m_szDisconnectedSessions[ 64 ];
        TCHAR m_szEstimatedSessionCapacity[ 64 ];
        TCHAR m_szResourceConstraint[ 64 ];
        TCHAR m_szRawSessionCapacity[ 64 ];
        TCHAR m_szName[ 64 ];
        TCHAR m_szCaption[ 64 ];
       
        
} ;



//=---------


class CWin32_TerminalServiceSetting : public Provider
{
    public:
        // Constructor/destructor
        //=======================

        CWin32_TerminalServiceSetting(LPCWSTR lpwszClassName, LPCWSTR lpwszNameSpace);
        virtual ~CWin32_TerminalServiceSetting();

    protected:
        // Reading Functions
        //============================
        virtual HRESULT EnumerateInstances(MethodContext*  pMethodContext, long lFlags = 0L);
        virtual HRESULT GetObject(CInstance* pInstance, long lFlags, CFrameworkQuery &Query);
        virtual HRESULT ExecQuery(MethodContext *pMethodContext, CFrameworkQuery& Query, long lFlags = 0L);

        // Writing Functions    
        //============================
        virtual HRESULT PutInstance(const CInstance& Instance, long lFlags = 0L);
        virtual HRESULT DeleteInstance(const CInstance& Instance, long lFlags = 0L);

        // Other Functions
        virtual HRESULT ExecMethod(const CInstance& Instance,
                        const BSTR bstrMethodName,
                        CInstance *pInParams,
                        CInstance *pOutParams,
                        long lFlags = 0L );

        HRESULT LoadPropertyValues( CInstance *pInstance, DWORD dwRequiredProperties);


        // TO DO: Declare any additional functions and accessor
        // functions for private data used by this class
        //===========================================================

    

    private:
        // All data members for CTerminalWinstation should be included here.  
    //    BOOL m_bInitialized;

        BOOL IsInList(const CHStringArray &asArray, LPCWSTR pszString);

        ISettingsComp *m_pSettings;
        POLICY_TS_MACHINE m_gpPolicy;
        TCHAR m_szServerName[ 64 ];
        TCHAR m_szMode[ 64 ];
        TCHAR m_szLicensingName[ 64 ];
        TCHAR m_szLicensingDescription[ 512 ];
        TCHAR m_szActiveDesktop[ 64 ];
        TCHAR m_szUserPerm[ 64 ];
        TCHAR m_szDeleteTempFolders[ 64 ];
        TCHAR m_szUseTempFolders[ 64 ];
        TCHAR m_szLogons[ 64 ];
        TCHAR m_szHelp[ 64 ];
        TCHAR m_szPropertyName[ 64 ];
        TCHAR m_szValue[ 64 ];
        TCHAR m_szChangeMode[ 64 ];
        TCHAR m_szSetPolicyPropertyName[ 64 ];
        TCHAR m_szLicensingType[ 64 ];
        TCHAR m_szAllowTSConnections[ 64 ];
        TCHAR m_szSetAllowTSConnections[ 64 ];
        TCHAR m_szSingleSession[ 64 ];
        TCHAR m_szSetSingleSession[ 64 ];
        TCHAR m_szProfilePath[ 64 ];
        TCHAR m_szHomeDirectory[ 64 ];
        TCHAR m_szDisableForcibleLogoff[ 64 ];
        TCHAR m_szSetProfilePath[ 64 ];
        TCHAR m_szSetHomeDirectory[ 64 ];
        TCHAR m_szAddDirectConnectLicenseServer[ 64 ];
        TCHAR m_szDeleteDirectConnectLicenseServer[ 64 ];
        TCHAR m_szLicenseServerName[ 64 ];
        TCHAR m_szDirectConnectLicenseServers[ 64 ];
        TCHAR m_szTimeZoneRedirection[ 64 ];
        TCHAR m_szSetTimeZoneRedirection[ 64 ]; 
        TCHAR m_szSetDisableForcibleLogoff[ 64 ];
} ;


//=---------


class CWin32_TSSessionDirectory : public Provider
{
    public:
        // Constructor/destructor
        //=======================

        CWin32_TSSessionDirectory(LPCWSTR lpwszClassName, LPCWSTR lpwszNameSpace);
        virtual ~CWin32_TSSessionDirectory();

    protected:
        // Reading Functions
        //============================
        virtual HRESULT EnumerateInstances(MethodContext*  pMethodContext, long lFlags = 0L);
        virtual HRESULT GetObject(CInstance* pInstance, long lFlags, CFrameworkQuery &Query );
        virtual HRESULT ExecQuery(MethodContext *pMethodContext, CFrameworkQuery& Query, long lFlags = 0L);

        // Writing Functions    
        //============================
        virtual HRESULT PutInstance(const CInstance& Instance, long lFlags = 0L);
        virtual HRESULT DeleteInstance(const CInstance& Instance, long lFlags = 0L);

        // Other Functions
        virtual HRESULT ExecMethod(const CInstance& Instance,
                        const BSTR bstrMethodName,
                        CInstance *pInParams,
                        CInstance *pOutParams,
                        long lFlags = 0L );

        HRESULT LoadPropertyValues( CInstance *pInstance, DWORD dwRequiredProperties);


        // TO DO: Declare any additional functions and accessor
        // functions for private data used by this class
        //===========================================================

    

    private:
        // All data members for CTerminalWinstation should be included here.  
    //    BOOL m_bInitialized;

        BOOL IsInList(const CHStringArray &asArray, LPCWSTR pszString);

   //     ADOConnection *m_pConnection;
        POLICY_TS_MACHINE m_gpPolicy;
        TCHAR m_szMode[ 64 ];
        TCHAR m_szSessionDirectoryActive[ 64 ];
        TCHAR m_szSessionDirectoryLocation[ 64 ];
        TCHAR m_szSessionDirectoryClusterName[ 64 ];
        TCHAR m_szSessionDirectoryExposeServerIP[ 64 ];
        TCHAR m_szSessionDirectoryIPAddress[ 64 ];
//        TCHAR m_szSessionDirectoryAdditionalParams[ 64 ];
        TCHAR m_szSetSessionDirectoryProperty[ 64 ];
        TCHAR m_szSetSessionDirectoryActive[ 64 ];
        TCHAR m_szSetSessionDirectoryExposeServerIP[ 64 ];
        TCHAR m_szPropertyName[ 64 ];
        TCHAR m_szValue[ 64 ];
        
} ;




//-----------------------Changed--------------------------

// struct to hold instance information


#endif

