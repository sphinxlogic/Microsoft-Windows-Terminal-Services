//Copyright (c) 1998 - 2001 Microsoft Corporation
#include "precomp.h"

#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0400
#endif

#include "wincrypt.h"
#include "tlsapip.h"
#include "lrwizapi.h"
#include "wincrypt.h"
#include "dlgproc.h"
#include "propdlgs.h"

#include "global.h"
#include "utils.h"
#include "fonts.h"

#define  ACTIVATIONMETHOD_KEY			"ACTIVATIONMETHOD"

CGlobal		*g_CGlobal = NULL;


BOOL   WINAPI   DllMain (HANDLE hInst,ULONG ul_reason_for_call,LPVOID lpReserved)
{ 
	switch(ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		if (g_CGlobal == NULL)
		{
			g_CGlobal = new CGlobal;
			if (g_CGlobal == NULL)
			{
				return FALSE;
			}

			SetInstanceHandle((HINSTANCE)hInst);
		}
		break;

	case DLL_PROCESS_DETACH:
		if (g_CGlobal != NULL)
		{
			delete g_CGlobal;
			g_CGlobal = NULL;
		}
		break;

	default:
		break;
	}
	
	return TRUE;
}




DWORD 
IsLicenseServerRegistered(HWND hWndParent, 
						  LPCTSTR pszLSName,
						  PDWORD pdwServerStatus)
{
	DWORD dwRetCode = ERROR_SUCCESS;
	
	GetGlobalContext()->SetLSName(pszLSName);

	if (!GetGlobalContext()->IsLSRunning())
	{		
		dwRetCode = LRWIZ_ERROR_LS_NOT_RUNNING;
		goto done;
	}	

	dwRetCode = GetGlobalContext()->IsLicenseServerRegistered(pdwServerStatus);

	if (dwRetCode == ERROR_SUCCESS && *pdwServerStatus == LSERVERSTATUS_UNREGISTER &&
		GetGlobalContext()->IsOnlineCertRequestCreated())
	{
		*pdwServerStatus = LSERVERSTATUS_WAITFORPIN;
	}

	GetGlobalContext()->FreeGlobal();

done:
	return dwRetCode;
}





DWORD
GetConnectionType(HWND hWndParent,
                    LPCTSTR pszLSName,
                    WIZCONNECTION* pConnectionType)
{
    DWORD dwRetCode = ERROR_SUCCESS;
    TCHAR lpBuffer[64];

    *pConnectionType = CONNECTION_DEFAULT;

    // Connect to the given LS Registry & read the current ConnectionType.
    GetGlobalContext()->SetLSName(pszLSName);
	
    dwRetCode = GetGlobalContext()->GetFromRegistry(ACTIVATIONMETHOD_KEY, lpBuffer, TRUE);
    
    if (dwRetCode == ERROR_SUCCESS)
    {
        if (_tcslen(lpBuffer) != 0)
        {
            *pConnectionType = (WIZCONNECTION) _ttoi(lpBuffer);

            if (*pConnectionType == CONNECTION_FAX)
                *pConnectionType = CONNECTION_DEFAULT;
        }
        else
            dwRetCode = ERROR_FILE_NOT_FOUND;
    }

	GetGlobalContext()->FreeGlobal();

    return dwRetCode;
}




DWORD 
StartWizard(
    HWND hWndParent, 
    WIZACTION WizAction,
    LPCTSTR pszLSName, 
    PBOOL pbRefresh
)
{
	DWORD			dwRetCode			= LRWIZ_SUCCESS;
    BOOL            bStatus             = TRUE;
    PageInfo        PageInfo            = {0};
    PROPSHEETPAGE   psp                 = {0};
    HPROPSHEETPAGE  ahpsp[NO_OF_PAGES]  = {0};
    PROPSHEETHEADER psh                 = {0};
	UINT			i = 0;
	DWORD			dwLSStatus = 0;
	
	GetGlobalContext()->SetWizAction(WizAction);

	GetGlobalContext()->SetLSName(pszLSName);

	if(!GetGlobalContext()->IsLSRunning())
	{
		LRMessageBox(hWndParent,IDS_ERR_LSCONNECT_FAILED,IDS_WIZARD_MESSAGE_TITLE);
		dwRetCode = LRWIZ_ERROR_LS_NOT_RUNNING;
		goto done;
	}

	dwRetCode = GetGlobalContext()->InitGlobal();
	if (dwRetCode != ERROR_SUCCESS)
	{
		LRMessageBox(hWndParent,dwRetCode,NULL,LRGetLastError());
		goto done;
	}

	dwRetCode = GetGlobalContext()->GetLSCertificates(&dwLSStatus);
	if (dwRetCode != ERROR_SUCCESS)
	{	
		LRMessageBox(hWndParent,dwRetCode,NULL,LRGetLastError());
		goto done;
	}	

	if (dwLSStatus == LSERVERSTATUS_UNREGISTER && GetGlobalContext()->IsOnlineCertRequestCreated())
	{
		dwLSStatus = LSERVERSTATUS_WAITFORPIN;
	}

	//
	// Show properties if WizAction is WIZACTION_SHOWPROPERTIES
	//
	if(WizAction == WIZACTION_SHOWPROPERTIES)
	{
		dwRetCode = ShowProperties(hWndParent);
		*pbRefresh = GetReFresh();
		return dwRetCode;
	}

	// verify the registry entries if the LS is already registered and the 
	// connection method is Internet
	if ((GetGlobalContext()->GetActivationMethod() == CONNECTION_INTERNET ||
		 GetGlobalContext()->GetActivationMethod() == CONNECTION_DEFAULT) 
		 && dwLSStatus == LSERVERSTATUS_REGISTER_INTERNET )
	{
		dwRetCode = GetGlobalContext()->CheckRequieredFields();
		if (dwRetCode != ERROR_SUCCESS)
		{
			LRMessageBox(hWndParent,dwRetCode,NULL,LRGetLastError());
			goto done;
		}
	}

	if (dwLSStatus == LSERVERSTATUS_WAITFORPIN)
	{
		GetGlobalContext()->SetWizAction(WIZACTION_CONTINUEREGISTERLS);
	}

	assert(dwLSStatus == LSERVERSTATUS_UNREGISTER ||
		   dwLSStatus == LSERVERSTATUS_WAITFORPIN ||
		   dwLSStatus == LSERVERSTATUS_REGISTER_INTERNET ||
		   dwLSStatus == LSERVERSTATUS_REGISTER_OTHER);


	GetGlobalContext()->SetLSStatus(dwLSStatus);

	//Create All the pages here

    // New Welcome page which explains the process, etc.
    switch (WizAction)
    {
        case (WIZACTION_REGISTERLS):
        {
            psp.dwSize              = sizeof( psp );
            psp.hInstance           = GetInstanceHandle();
            psp.lParam              = (LPARAM)&PageInfo;	
	        psp.pfnDlgProc          = SimpleWelcomeDlgProc;
            psp.dwFlags             = PSP_DEFAULT | PSP_HIDEHEADER;
            psp.pszTemplate         = MAKEINTRESOURCE(IDD_WELCOME_ACTIVATION);
            ahpsp[PG_NDX_WELCOME]	= CreatePropertySheetPage( &psp );
            break;
        }
        
        case (WIZACTION_CONTINUEREGISTERLS):    
        {
            psp.dwSize              = sizeof( psp );
            psp.hInstance           = GetInstanceHandle();
            psp.lParam              = (LPARAM)&PageInfo;	
	        psp.pfnDlgProc          = ComplexWelcomeDlgProc;
            psp.dwFlags             = PSP_DEFAULT | PSP_HIDEHEADER;
            psp.pszTemplate         = MAKEINTRESOURCE(IDD_WELCOME_ACTIVATION);
            ahpsp[PG_NDX_WELCOME]	= CreatePropertySheetPage( &psp );
            break;
        }
        case (WIZACTION_DOWNLOADLKP):    
        case (WIZACTION_DOWNLOADLASTLKP):    
        {
            psp.dwSize              = sizeof( psp );
            psp.hInstance           = GetInstanceHandle();
            psp.lParam              = (LPARAM)&PageInfo;	
	        psp.pfnDlgProc          = ComplexWelcomeDlgProc;
            psp.dwFlags             = PSP_DEFAULT | PSP_HIDEHEADER;
            psp.pszTemplate         = MAKEINTRESOURCE(IDD_WELCOME_CLIENT_LICENSING);
            ahpsp[PG_NDX_WELCOME]	= CreatePropertySheetPage( &psp );
            break;
        }
        case (WIZACTION_REREGISTERLS):    
        {
            psp.dwSize              = sizeof( psp );
            psp.hInstance           = GetInstanceHandle();
            psp.lParam              = (LPARAM)&PageInfo;	
	        psp.pfnDlgProc          = ComplexWelcomeDlgProc;
            psp.dwFlags             = PSP_DEFAULT | PSP_HIDEHEADER;
            psp.pszTemplate         = MAKEINTRESOURCE(IDD_WELCOME_REACTIVATION);
            ahpsp[PG_NDX_WELCOME]	= CreatePropertySheetPage( &psp );
            break;
        }
        default:
        {
            psp.dwSize              = sizeof( psp );
            psp.hInstance           = GetInstanceHandle();
            psp.lParam              = (LPARAM)&PageInfo;	
	        psp.pfnDlgProc          = ComplexWelcomeDlgProc;
            psp.dwFlags             = PSP_DEFAULT | PSP_HIDEHEADER;
            psp.pszTemplate         = MAKEINTRESOURCE(IDD_WELCOME);
            ahpsp[PG_NDX_WELCOME]	= CreatePropertySheetPage( &psp );
            break;
        }
    }

	// New page for choosing the Mode of Registration
    psp.dwSize              = sizeof( psp );
    psp.dwFlags             = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance           = GetInstanceHandle();
    psp.lParam              = (LPARAM)&PageInfo;	
	psp.pfnDlgProc          = GetModeDlgProc;
    psp.pszHeaderTitle		= MAKEINTRESOURCE( IDS_TITLE20 );
    psp.pszHeaderSubTitle	= MAKEINTRESOURCE( IDS_SUBTITLE20 );
    psp.pszTemplate         = MAKEINTRESOURCE( IDD_DLG_GETREGMODE );
    ahpsp[PG_NDX_GETREGMODE]= CreatePropertySheetPage( &psp );


	//
	//Customer Information(2) page for CA Request(Online/Offline)
	//
	memset(&psp,0,sizeof(psp));
	psp.dwSize					= sizeof( psp );
    psp.dwFlags					= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance				= GetInstanceHandle();
    psp.lParam					= (LPARAM)&PageInfo;
	psp.pfnDlgProc				= ContactInfo1DlgProc;	
    psp.pszHeaderTitle			= MAKEINTRESOURCE( IDS_TITLE4 );
    psp.pszHeaderSubTitle		= MAKEINTRESOURCE( IDS_SUBTITLE4 );
    psp.pszTemplate				= MAKEINTRESOURCE( IDD_CONTACTINFO1 );
    ahpsp[PG_NDX_CONTACTINFO1]	= CreatePropertySheetPage( &psp );


	//
	//Customer Information(1) page for CA Request(Online/Offline)
	//
	memset(&psp,0,sizeof(psp));
	psp.dwSize					= sizeof( psp );
    psp.dwFlags					= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance				= GetInstanceHandle();
    psp.lParam					= (LPARAM)&PageInfo;
	psp.pfnDlgProc				= ContactInfo2DlgProc;	
    psp.pszHeaderTitle			= MAKEINTRESOURCE( IDS_TITLE3 );
    psp.pszHeaderSubTitle		= MAKEINTRESOURCE( IDS_SUBTITLE3 );
    psp.pszTemplate				= MAKEINTRESOURCE( IDD_CONTACTINFO2 );
    ahpsp[PG_NDX_CONTACTINFO2]	= CreatePropertySheetPage( &psp );	

	
#ifdef XXX
	//
	//Processing Request page(Online)
	//
	memset(&psp,0,sizeof(psp));
	psp.dwSize					= sizeof( psp );
    psp.dwFlags					= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance				= GetInstanceHandle();
    psp.lParam					= (LPARAM)&PageInfo;
	psp.pfnDlgProc				= ProcessingDlgProc;	
    psp.pszHeaderTitle			= MAKEINTRESOURCE( IDS_TITLE5 );    
	psp.pszHeaderSubTitle		= MAKEINTRESOURCE( IDS_SUBTITLE5 );
    psp.pszTemplate				= MAKEINTRESOURCE( IDD_PROCESSING );
    ahpsp[PG_NDX_PROCESSING]	= CreatePropertySheetPage( &psp );
#endif


	//
	//Registration Complete page for CA Request(Online/Offline)
	//
	memset(&psp,0,sizeof(psp));
	psp.dwSize					= sizeof( psp );
    psp.dwFlags					= PSP_DEFAULT | PSP_HIDEHEADER;
    psp.hInstance				= GetInstanceHandle();
    psp.lParam					= (LPARAM)&PageInfo;
	psp.pfnDlgProc				= ProgressDlgProc;    
    psp.pszTemplate				= MAKEINTRESOURCE( IDD_PROGRESS );
    ahpsp[PG_NDX_PROGRESS]		= CreatePropertySheetPage( &psp );

	memset(&psp,0,sizeof(psp));
	psp.dwSize					= sizeof( psp );
    psp.dwFlags					= PSP_DEFAULT | PSP_HIDEHEADER;
    psp.hInstance				= GetInstanceHandle();
    psp.lParam					= (LPARAM)&PageInfo;
	psp.pfnDlgProc				= Progress2DlgProc;
    psp.pszTemplate				= MAKEINTRESOURCE( IDD_PROGRESS2 );
    ahpsp[PG_NDX_PROGRESS2]	    = CreatePropertySheetPage( &psp );



	//
	//Certificate PIN page for CA Request(Online)
	//
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= PINDlgProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE9 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE9 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_PIN );
    ahpsp[PG_NDX_DLG_PIN]				= CreatePropertySheetPage( &psp );

	//
	//Choose Program page for CH Request(Online/Offline)
	//
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= CustInfoLicenseType;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE10 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE10 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_LICENSETYPE );
    ahpsp[PG_NDX_CH_REGISTER_1]			= CreatePropertySheetPage( &psp );

	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= CHRegisterDlgProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE12 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE12 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_CH_REGISTER );
    ahpsp[PG_NDX_CH_REGISTER]		    = CreatePropertySheetPage( &psp );


	//
	// Options after registering
	//
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= ContinueReg;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE18 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE18 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_CONTINUEREG );
    ahpsp[PG_NDX_CONTINUEREG]			= CreatePropertySheetPage( &psp );


	// New Dialog Box to complete the Telephone Registration
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= TelRegProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE19 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE19 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_TELREG);
    ahpsp[PG_NDX_TELREG]	   		    = CreatePropertySheetPage( &psp );


	// New Dialog Box to complete the Telephone LKP stuff
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= TelLKPProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE21 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE21 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_TELLKP);
    ahpsp[PG_NDX_TELLKP]	   		    = CreatePropertySheetPage( &psp );


	// New Dialog Box to complete the Retail SPK Implementation
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= RetailSPKProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE22 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE22 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_RETAILSPK );
    ahpsp[PG_NDX_RETAILSPK]	   		    = CreatePropertySheetPage( &psp );


	// New Dialog Box to complete the Cert Log Infor (before re-issuing/revoking certs)
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= CertLogProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE24 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE24 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_CERTLOG_INFO );
    ahpsp[PG_NDX_CERTLOG]   		    = CreatePropertySheetPage( &psp );


	// Telephone Revocation
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= ConfRevokeProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE25 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE25 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_CONFREVOKE );
    ahpsp[PG_NDX_CONFREVOKE]   		    = CreatePropertySheetPage( &psp );

	// Telephone re-issue
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= TelReissueProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE26 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE26 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_TELREG_REISSUE );
    ahpsp[PG_NDX_TELREG_REISSUE] 	    = CreatePropertySheetPage( &psp );

	// WWW re-issue
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= WWWReissueProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE26 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE26 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_WWWREG_REISSUE );
    ahpsp[PG_NDX_WWWREG_REISSUE] 	    = CreatePropertySheetPage( &psp );


	// Telephone Country/Region
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= CountryRegionProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE27 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE27 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_COUNTRYREGION );
    ahpsp[PG_NDX_COUNTRYREGION] 	    = CreatePropertySheetPage( &psp );


	// WWW Registration
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= WWWRegProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE28 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE28 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_WWWREG);
    ahpsp[PG_NDX_WWWREG]		 	    = CreatePropertySheetPage( &psp );

	// WWW LKP Download
	memset(&psp,0,sizeof(psp));
	psp.dwSize							= sizeof( psp );
    psp.dwFlags							= PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance						= GetInstanceHandle();
    psp.lParam							= (LPARAM)&PageInfo;
	psp.pfnDlgProc						= WWWLKPProc;
    psp.pszHeaderTitle					= MAKEINTRESOURCE( IDS_TITLE29 );
	psp.pszHeaderSubTitle				= MAKEINTRESOURCE( IDS_SUBTITLE29 );
    psp.pszTemplate						= MAKEINTRESOURCE( IDD_DLG_WWWLKP);
    ahpsp[PG_NDX_WWWLKP]			    = CreatePropertySheetPage( &psp );

    //Add the other welcome screens. Their order will differ depending
    //on the wiz action, because the relevant one will need to be the
    //first dialog of the wizard...the others will be added to the end
    switch (WizAction)
    {
        case (WIZACTION_REGISTERLS):    
        case (WIZACTION_CONTINUEREGISTERLS):    
        {
            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME_CLIENT_LICENSING);
            ahpsp[PG_NDX_WELCOME_1]			    = CreatePropertySheetPage( &psp );

            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME_REACTIVATION);
            ahpsp[PG_NDX_WELCOME_2]			    = CreatePropertySheetPage( &psp );

            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME);
            ahpsp[PG_NDX_WELCOME_3]			    = CreatePropertySheetPage( &psp );
            break;
        }
        case (WIZACTION_DOWNLOADLKP):    
        case (WIZACTION_DOWNLOADLASTLKP):    
        {
            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME_ACTIVATION);
            ahpsp[PG_NDX_WELCOME_1]			    = CreatePropertySheetPage( &psp );

            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME_REACTIVATION);
            ahpsp[PG_NDX_WELCOME_2]			    = CreatePropertySheetPage( &psp );

            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME);
            ahpsp[PG_NDX_WELCOME_3]			    = CreatePropertySheetPage( &psp );
            break;
        }
        case (WIZACTION_REREGISTERLS):    
        {
            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME_ACTIVATION);
            ahpsp[PG_NDX_WELCOME_1]			    = CreatePropertySheetPage( &psp );

            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME_CLIENT_LICENSING);
            ahpsp[PG_NDX_WELCOME_2]			    = CreatePropertySheetPage( &psp );

            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME);
            ahpsp[PG_NDX_WELCOME_3]			    = CreatePropertySheetPage( &psp );
            break;
        }
        default:
        {
            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME_ACTIVATION);
            ahpsp[PG_NDX_WELCOME_1]			    = CreatePropertySheetPage( &psp );

            memset(&psp,0,sizeof(psp));
	        psp.dwSize							= sizeof( psp );
            psp.dwFlags							= PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance						= GetInstanceHandle();
            psp.lParam							= (LPARAM)&PageInfo;
	        psp.pfnDlgProc						= ComplexWelcomeDlgProc;
            psp.pszTemplate						= MAKEINTRESOURCE(IDD_WELCOME_CLIENT_LICENSING);
            ahpsp[PG_NDX_WELCOME_2]			    = CreatePropertySheetPage( &psp );

            memset(&psp,0,sizeof(psp));
            psp.dwSize                          = sizeof( psp );
            psp.dwFlags                         = PSP_DEFAULT | PSP_HIDEHEADER;
            psp.hInstance                       = GetInstanceHandle();
            psp.lParam                          = (LPARAM)&PageInfo;
            psp.pfnDlgProc                      = ComplexWelcomeDlgProc;
            psp.pszTemplate                     = MAKEINTRESOURCE(IDD_WELCOME_REACTIVATION);
            ahpsp[PG_NDX_WELCOME_3]             = CreatePropertySheetPage( &psp );
            break;
        }
    }

    //
    // Enter license code (e.g. select type licenses)
    //
    memset(&psp,0,sizeof(psp));
    psp.dwSize                          = sizeof( psp );
    psp.dwFlags                         = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.hInstance                       = GetInstanceHandle();
    psp.lParam                          = (LPARAM)&PageInfo;
    psp.pfnDlgProc                      = EnterCustomLicenseProc;
    psp.pszHeaderTitle                  = MAKEINTRESOURCE( IDS_ENTER_LICENSE_WIZARD_HEADER );
    psp.pszHeaderSubTitle               = MAKEINTRESOURCE( IDS_ENTER_LICENSE_WIZARD_SUBHEADER );
    psp.pszTemplate                     = MAKEINTRESOURCE( IDD_DLG_ENTER_LIC );
    ahpsp[PG_NDX_ENTERLICENSE]          = CreatePropertySheetPage( &psp );


    psh.dwFlags             = PSH_WIZARD | PSH_WIZARD97 | PSH_WATERMARK | PSH_HEADER;

    //psh.pszbmWatermark      = 0;
    //psh.pszbmHeader         = 0;
    psh.pszbmWatermark      = MAKEINTRESOURCE(IDB_CLIENT_CONN);
    psh.pszbmHeader         = MAKEINTRESOURCE(IDB_CLIENT_CONN_HDR);

    psh.dwSize              = sizeof( psh );
    psh.hInstance           = GetInstanceHandle();
    psh.hwndParent          = hWndParent;
    psh.pszCaption          = MAKEINTRESOURCE( IDS_TITLE4 );
    psh.phpage              = ahpsp;
    psh.nStartPage          = 0;
    psh.nPages              = NO_OF_PAGES;
    PageInfo.TotalPages     = NO_OF_PAGES;


    //
    // Create the bold fonts.
    // 
    SetupFonts( GetInstanceHandle(), NULL, &PageInfo.hBigBoldFont, &PageInfo.hBoldFont );

    //
    // Validate all the pages.
    //
    for(i = 0; i < NO_OF_PAGES; i++ )
    {
        if( ahpsp[i] == 0 )
        {
            bStatus = FALSE;
        }
    }

    //
    // Display the wizard.
    //
    if( bStatus )
    {   
        if( PropertySheet( &psh ) == -1 )
        {
            bStatus = FALSE;
        }
    }

    if( !bStatus )
    {
        //
        // Manually destroy the pages if something failed.
        //
        for(i = 0; i < psh.nPages; i++)
        {
            if( ahpsp[i] )
            {
                DestroyPropertySheetPage( ahpsp[i] );
            }
        }
    }

    //
    // Destroy the fonts that were created.
    //
    DestroyFonts( PageInfo.hBigBoldFont, PageInfo.hBoldFont );

done:
	if(!bStatus)
	{
		LRMessageBox(hWndParent,IDS_ERR_CREATE_FAILED,IDS_WIZARD_MESSAGE_TITLE);
		dwRetCode = LRWIZ_ERROR_CREATE_FAILED;
	}

	if (dwRetCode == 0 || dwRetCode == LRWIZ_SUCCESS)
	{
		*pbRefresh = GetReFresh();
	}

	GetGlobalContext()->FreeGlobal();

    return dwRetCode;
}


DWORD ShowProperties(HWND hWndParent)
{
	DWORD			dwRetCode = ERROR_SUCCESS;
	BOOL            bStatus             = TRUE;    
    PROPSHEETPAGE   psp                 = {0};
    HPROPSHEETPAGE  ahpsp[NO_OF_PROP_PAGES]  = {0};
    PROPSHEETHEADER psh                 = {0};
	UINT i = 0;

	//
	//Create All the pages here
	//

	// Registration Mode Page
	memset(&psp,0,sizeof(psp));
    psp.dwSize              = sizeof( psp );
    psp.dwFlags             = PSP_DEFAULT | PSP_USETITLE | PSP_PREMATURE ; 
    psp.hInstance           = GetInstanceHandle();
    psp.lParam              = NULL;	
	psp.pfnDlgProc          = PropModeDlgProc;
    psp.pszTitle			= MAKEINTRESOURCE( IDS_TITLE36 );
	psp.pszHeaderTitle		= MAKEINTRESOURCE( IDS_TITLE36 );
    psp.pszTemplate         = MAKEINTRESOURCE( IDD_DLG_PROP_MODE);
	
    ahpsp[PG_NDX_PROP_MODE]= CreatePropertySheetPage( &psp );
	
	// Customer Information (I) page
	memset(&psp,0,sizeof(psp));
	psp.dwSize					= sizeof( psp );
    psp.dwFlags					= PSP_DEFAULT |  PSP_USETITLE | PSP_PREMATURE;
    psp.hInstance				= GetInstanceHandle();
    psp.lParam					= NULL;
	psp.pfnDlgProc				= PropCustInfoADlgProc;
    psp.pszHeaderTitle			= MAKEINTRESOURCE( IDS_TITLE38 );
	psp.pszTitle				= MAKEINTRESOURCE( IDS_TITLE38 );
    psp.pszTemplate				= MAKEINTRESOURCE( IDD_DLG_PROP_CUSTINFO_a);
    ahpsp[PG_NDX_PROP_CUSTINFO_a]	= CreatePropertySheetPage( &psp );


	// Customer Information (II) page
	memset(&psp,0,sizeof(psp));
	psp.dwSize					= sizeof( psp );
    psp.dwFlags					= PSP_DEFAULT |  PSP_USETITLE | PSP_PREMATURE;
    psp.hInstance				= GetInstanceHandle();
    psp.lParam					= NULL;
	psp.pfnDlgProc				= PropCustInfoBDlgProc;
    psp.pszHeaderTitle			= MAKEINTRESOURCE( IDS_TITLE39 );
	psp.pszTitle				= MAKEINTRESOURCE( IDS_TITLE39 );
    psp.pszTemplate				= MAKEINTRESOURCE( IDD_DLG_PROP_CUSTINFO_b);
    ahpsp[PG_NDX_PROP_CUSTINFO_b]	= CreatePropertySheetPage( &psp );


	psh.dwFlags             = PSH_DEFAULT | PSH_PROPTITLE | PSH_NOAPPLYNOW| PSH_NOCONTEXTHELP;
    psh.dwSize              = sizeof( psh );
    psh.hInstance           = GetInstanceHandle();
    psh.hwndParent          = hWndParent;
    psh.pszCaption          = MAKEINTRESOURCE( IDS_PROPERTIES_TITLE );
    psh.phpage              = ahpsp;
    psh.nStartPage          = 0;
    psh.nPages              = NO_OF_PROP_PAGES;    

    //
    // Validate all the pages.
    //
    for( i = 0; i < NO_OF_PROP_PAGES; i++ )
    {
        if( ahpsp[i] == 0 )
        {
            bStatus = FALSE;
        }
    }

    //
    // Display the wizard.
    //
    if( bStatus )
    {   
        if( PropertySheet( &psh ) == -1 )
        {
            bStatus = FALSE;
        }
    }

    if( !bStatus )
    {
        //
        // Manually destroy the pages if something failed.
        //
        for(i = 0; i < psh.nPages; i++)
        {
            if( ahpsp[i] )
            {
                DestroyPropertySheetPage( ahpsp[i] );
            }
        }
    }    

	if(!bStatus)
	{
		LRMessageBox(hWndParent,IDS_ERR_CREATE_FAILED,IDS_WIZARD_MESSAGE_TITLE);
		dwRetCode = LRWIZ_ERROR_CREATE_FAILED;
	}

	return dwRetCode;
}
