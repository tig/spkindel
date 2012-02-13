// ExternalControlProcessor.h : Declaration of the ExternalControlProcessor.h

#pragma once


#include "resource.h"       // main symbols
#include "driverutil.h"

#pragma comment(lib, "sysuuid.lib")

/////////////////////////////////////////////////////////////////////////////
// CExternalControlProcessor
class ATL_NO_VTABLE CExternalControlProcessor : 
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CExternalControlProcessor, &CLSID_ExternalControlProcessor>,
	public CPremiseDriverImpl
{
public:
	CExternalControlProcessor()
	{
	}


DECLARE_REGISTRY_RESOURCEID(IDR_EXTERNALCONTROLPROCESSOR)
DECLARE_NOT_AGGREGATABLE(CExternalControlProcessor)
DECLARE_PROTECT_FINAL_CONSTRUCT()


BEGIN_COM_MAP(CExternalControlProcessor)
	COM_INTERFACE_ENTRY(IObjectWithSite)
	COM_INTERFACE_ENTRY(IPremiseNotify)
END_COM_MAP()


BEGIN_NOTIFY_MAP(CExternalControlProcessor)
END_NOTIFY_MAP() 


	HRESULT CreateControllerForSite(IPremiseObject* pObject, IObjectWithSite** ppSite, bool bFirstTime)
	{
		HRESULT hr;
		CComBSTR bstr;
		pObject->get_Name(&bstr);

		//ATLTRACE2("CExternalControlProcessor::CreateControllerForSite(0x%p '%S')\n", pObject, bstr);
		if (IsObjectOfExplicitType(pObject, XML_Device_ExternalProcessor))
		{
			hr = CExternalProcessor::CreateInstance(ppSite);
		}
		return hr;
	}
};


OBJECT_ENTRY_AUTO(__uuidof(ExternalControlProcessor), CExternalControlProcessor)
