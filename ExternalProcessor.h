//  : Declaration of the 

#pragma once


#include "resource.h"       // main symbols
#include "driverutil.h"
#include "VirtualDevice.h"

#define XML_Device_ExternalProcessor	L"sys://Schema/KindelSystems/ExternalProcessor"


static const BYTE s_rgCmdDelimiter[2] = {0x0D, 0x0A};

/////////////////////////////////////////////////////////////////////////////
// 

class ATL_NO_VTABLE CExternalProcessor : 
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CExternalProcessor>,
	public CPremiseBufferedPortDevice
{
public:
	DECLARE_NO_REGISTRY()
	DECLARE_NOT_AGGREGATABLE(CExternalProcessor)
	DECLARE_PROTECT_FINAL_CONSTRUCT()

	//have to play this trick with CAdapt because CSimpleArray takes the 
	//address of its items.  CComPtr overloads the & operator and ASSERTS that the
	//value is NULL.  This causes various ASSERTS in normal situations
	typedef CAdapt<CComPtr<IObjectWithSite> > SPIOWS;
	CSimpleArray<SPIOWS> m_vecVirtualDevices;

protected:
	PROTECT(m_csBPD, CComBSTR, m_bstrWatchdogCommand);

public:
	CExternalProcessor()
	{
		SetTextModeTerminators((BYTE*)s_rgCmdDelimiter, sizeof(s_rgCmdDelimiter));
	}

BEGIN_COM_MAP(CExternalProcessor)
	COM_INTERFACE_ENTRY(IObjectWithSite)
	COM_INTERFACE_ENTRY(IPremisePortCallback)
	COM_INTERFACE_ENTRY(IPremiseNotify)
END_COM_MAP()

	HRESULT OnBrokerAttach();
	HRESULT OnBrokerDetach();
	STDMETHOD(OnObjectCreated)(IPremiseObject *pContainer, IPremiseObject *pCreatedObject);
	STDMETHOD(OnObjectDeleted)(IPremiseObject *pContainer, IPremiseObject *pDeletedObject);
	HRESULT AddVirutalDevice(IPremiseObject* pSite, bool bFirstTime);
	HRESULT RemoveVirutalDevice(IPremiseObject* pSite);
	HRESULT OnConfigurePort(IPremiseObject* pPort);
	bool OnWatchDog();
	void ProcessLine(LPCSTR psz);
	HRESULT OnDeviceState(DEVICE_STATE ps);

BEGIN_NOTIFY_MAP(CExternalProcessor)
	//OnNetworkChanged is implemented in CPremiseBufferedPortDevice
	NOTIFY_PROPERTY(L"Network", OnNetworkChanged) 
	NOTIFY_PROPERTY(L"EnableLogging", OnLoggingChanged) 
	NOTIFY_PROPERTY(L"WatchdogCommand", OnWatchdogCommandChanged) 
END_NOTIFY_MAP() 

	HRESULT STDMETHODCALLTYPE OnWatchdogCommandChanged(IPremiseObject *pObject, VARIANT newValue);
};



