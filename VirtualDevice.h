//  : Declaration of the 

#pragma once


#include "resource.h"       // main symbols
#include <driverutil.h>

#define XML_Device_VirtualDevice	L"sys://Schema/ExternalControlProcessor/VirtualDevice"

/////////////////////////////////////////////////////////////////////////////
// 

class ATL_NO_VTABLE CVirtualDevice : 
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CVirtualDevice>,
	public CPremiseSubscriber,
	IPremiseHandlePort
{
public:
	DECLARE_NO_REGISTRY()
	DECLARE_NOT_AGGREGATABLE(CVirtualDevice)
	DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CVirtualDevice)
	COM_INTERFACE_ENTRY(IPremiseHandlePort)
	COM_INTERFACE_ENTRY(IPremisePort)
	COM_INTERFACE_ENTRY(IObjectWithSite)
	COM_INTERFACE_ENTRY(IPremiseNotify)
END_COM_MAP()

	CComPtr<IPremisePortCallback>	m_spCB;
	DWORD		m_dwCookie;
	CComBSTR	m_bstrCommandPrefix;
	CComBSTR	m_bstrFile;
	bool		m_bOpen;

	CVirtualDevice()
	{
		//ATLTRACE2("CVirtualDevice constructor\n");
		m_dwCookie = 0;
		m_bOpen = FALSE;
	}
	
	~CVirtualDevice()
	{
	}

	// Overrides for CPremiseSubscriber
	HRESULT OnBrokerDetach();
	HRESULT OnBrokerAttach();

	//
	// IPremisePort methods.
	//
	STDMETHOD(Write)(const void *pv, ULONG cb, ULONG *pcbWritten);
	STDMETHOD(OpenPort)(IPremisePortCallback* pc);
	STDMETHOD(ClosePort)();
	STDMETHOD(ClosePortEx)(boolean bImmediate);
	STDMETHOD(GetOpenStatus)(long* pl);
	STDMETHOD(GetHandle)(long* pid);//gets a number which can uniquely identify this port
	STDMETHOD(GetDescription)(BSTR* pbstrDesc);
	STDMETHOD(SetCookie)(DWORD dwCookie);
	STDMETHOD(GetCookie)(DWORD* pdwCookie);
	STDMETHOD(GetCallback)(IPremisePortCallback** pCallback);
	STDMETHOD(SetCallback)(IPremisePortCallback* pc);

	//
	// IPremiseHandlePort methods
	//
	STDMETHOD(SetupPort)(LPCOLESTR pszFile, DWORD dwDesiredAccess, DWORD dwShareMode, 
		DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes);
	STDMETHOD(SetPortSpy)(IUnknown* pSite);


BEGIN_NOTIFY_MAP(CVirtualDevice)
	//OnNetworkChanged is implemented in CPremiseBufferedPortDevice
//	NOTIFY_PROPERTY(L"EnableLogging", OnLoggingChanged) 
	//add other properties here
//These are example properties.  You should edit .xml and add properties
//for your particular device.  Then edit these macros and created property change handlers
//for them
	NOTIFY_PROPERTY(L"CommandPrefix", OnCommandPrefixChanged) 
	NOTIFY_PROPERTY(L"Open", OnOpenChanged) 
END_NOTIFY_MAP() 

	HRESULT STDMETHODCALLTYPE OnCommandPrefixChanged(IPremiseObject *pObject, VARIANT newValue);
	HRESULT STDMETHODCALLTYPE OnOpenChanged(IPremiseObject *pObject, VARIANT newValue);

};



