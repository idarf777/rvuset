//
// (C)2013 Shizentai Factory Co.
//  All rights reserved.
//

#include "stdafx.h"
#include <iostream>

#include <windows.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <setupapi.h>

#pragma comment( lib, "hid.lib" )
#pragma comment( lib, "setupapi.lib" )

using namespace std;

#define VENDER_ID		0x22ea
#define PRODUCT_ID	0x0006
#define DEVICE_USAGE	0xff0001
#define INPUT_SIZE	65
#define OUTPUT_SIZE	65
#define N_PIN				12
enum EEPROM
{
	EPR_MODE = 0,
	EPR_VALUE = 1,
	EPR_MODIFIER = 2
};


HANDLE openHidCtrlDevice()
{
	GUID guidHid;

	HidD_GetHidGuid( &guidHid );
	HDEVINFO hDevList = SetupDiGetClassDevs( &guidHid, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE );
	if ( !hDevList )
	{
		cerr << "no HIDs" << endl;
		return NULL;
	}

	HANDLE rc = INVALID_HANDLE_VALUE;
	for ( DWORD dwIndex = 0;  rc == INVALID_HANDLE_VALUE;  dwIndex++ )
	{
		SP_DEVICE_INTERFACE_DATA  devInfoData;
		ZeroMemory( &devInfoData, sizeof(devInfoData) );
		devInfoData.cbSize = sizeof(devInfoData);
		if ( !SetupDiEnumDeviceInterfaces( hDevList, NULL, &guidHid, dwIndex, &devInfoData ) )
			break;
		DWORD dwLength = 0;
		SetupDiGetDeviceInterfaceDetail( hDevList, &devInfoData, NULL, 0, &dwLength, NULL );
		PSP_DEVICE_INTERFACE_DETAIL_DATA pDevDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, dwLength );
		pDevDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		DWORD dwRequired = 0;
		while ( SetupDiGetDeviceInterfaceDetail( hDevList, &devInfoData, pDevDetail, dwLength, &dwRequired, NULL ) )
		{
			//wcout << pDevDetail->DevicePath << endl;

			HANDLE hDev = CreateFile( pDevDetail->DevicePath, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, NULL, NULL );
			if ( hDev == INVALID_HANDLE_VALUE )
				break;
			while ( 1 )
			{
				HIDD_ATTRIBUTES attr;
				if ( !HidD_GetAttributes( hDev, &attr ) )
					break;
				if ( attr.VendorID != VENDER_ID  ||  attr.ProductID != PRODUCT_ID )
					break;

				PHIDP_PREPARSED_DATA pPreparsed;
				if ( !HidD_GetPreparsedData( hDev, &pPreparsed ) )
					break;
				HIDP_CAPS caps;
				if ( HidP_GetCaps( pPreparsed, &caps ) != HIDP_STATUS_SUCCESS )
					break;
				if ( ((caps.UsagePage<<8) | caps.Usage) != DEVICE_USAGE )
					break;

				// HID����f�o�C�X �I�[�v������
				rc = hDev;

				break;
			}

			if ( rc == INVALID_HANDLE_VALUE )
				CloseHandle( hDev );
			break;
		}
		HeapFree( GetProcessHeap(), 0, pDevDetail );
	}
	SetupDiDestroyDeviceInfoList( hDevList );

	return rc;
}

bool isValidDevice( HANDLE hDev )
{
	BYTE bufOUT[ OUTPUT_SIZE ];
	ZeroMemory( bufOUT, sizeof(bufOUT) );
	bufOUT[ 1 ] = 0x56;

	DWORD dwProcessed = 0;
	if ( WriteFile( hDev, bufOUT, OUTPUT_SIZE, &dwProcessed, NULL ) )
	{
		BYTE bufIN[ INPUT_SIZE ];
		ZeroMemory( bufIN, sizeof(bufIN) );
		dwProcessed = 0;
		if ( ReadFile( hDev, bufIN, INPUT_SIZE, &dwProcessed, NULL ) )
		{
			if ( bufIN[ 1 ] == 'V'  &&  bufIN[ 2 ] == '0' )
				return true;
		}
	}
	return false;
}

bool loadIniFile( BYTE pImg[ OUTPUT_SIZE ], LPCTSTR pFilename )
{
	ZeroMemory( pImg, OUTPUT_SIZE );
	pImg[ 1 ] = 0x80;

	TCHAR szApp[ 8 ];
	TCHAR szValue[ 3 ];
	TCHAR szMod[ 3 ];
	for ( int pin=0; pin<N_PIN; pin++ )
	{
		BYTE* cur = &pImg[ pin*3+2 ];
		wsprintf( szApp, _T("PIN%d"), pin+1 );
		int mode = (int)GetPrivateProfileInt( szApp, _T("MODE"), -1, pFilename );
		cur[ 0 ] = mode;
		if ( mode < 0 )
			continue;
		GetPrivateProfileString( szApp, _T("VALUE"), _T(""), szValue, ARRAYSIZE(szValue), pFilename );
		GetPrivateProfileString( szApp, _T("MODIFIER"), _T(""), szMod, ARRAYSIZE(szMod), pFilename );
		TCHAR* p;
		errno = 0;
		cur[ 1 ] = (BYTE)_tcstol( szValue, &p, 16 );
		if ( errno != 0 )
			return false;
		cur[ 2 ] = (BYTE)_tcstol( szMod, &p, 16 );
		if ( errno != 0 )
			return false;
	}
	return true;
}

int _tmain(int argc, _TCHAR* argv[])
{
	if ( argc < 2 )
	{
		cerr << "REVIVE USB keycode setter  (C)2013 Shizentai Factory Co." << endl;
		cerr << "Usage: rvuset <ini file>" << endl;
		return 0;
	}
	DWORD dwLength = GetFullPathName( argv[ 1 ], 0, NULL, NULL );
	if ( dwLength == 0 )
	{
		cerr << "cannot read specified file" << endl;
		return -1;
	}
	TCHAR* pFilename = new TCHAR[ dwLength ];
	GetFullPathName( argv[ 1 ], dwLength, pFilename, NULL );
	BYTE imgROM[ OUTPUT_SIZE ];
	bool isLoaded = loadIniFile( imgROM, pFilename );
	delete[] pFilename;
	if ( !isLoaded )
	{
		cerr << "load error" << endl;
		return -2;
	}

	HANDLE hDev = openHidCtrlDevice();
	if ( hDev == INVALID_HANDLE_VALUE )
	{
		cerr << "no connected device" << endl;
		return -3;
	}

	int rc = 0;
	if ( isValidDevice( hDev ) )
	{
		DWORD dwProcessed = 0;
		if ( !WriteFile( hDev, imgROM, OUTPUT_SIZE, &dwProcessed, NULL ) )
		{
			cerr << "cannot write to device" << endl;
			rc = -4;
		}
	}
	else
	{
		cerr << "invalid version" << endl;
		rc = -5;
	}
	CloseHandle( hDev );

	return rc;
}
