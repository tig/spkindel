// VirtualDevice.cpp : Implementation of CVirtualDevice

#include "stdafx.h"
#include "VirtualDevice.h"


/////////////////////////////////////////////////////////////////////////////
// CVirtualDevice

HRESULT CVirtualDevice::OnCommandPrefixChanged(IPremiseObject *pObject, VARIANT newValue)
{
	ATLTRACE2("[%S]CVirtualDevice::OnCommandPrefixChanged\n", m_bstrCommandPrefix);
	if (newValue.vt != VT_BSTR)
		m_bstrCommandPrefix = L"unknown";
	else
		m_bstrCommandPrefix = newValue.bstrVal;
	ATLTRACE2("  %S\n", newValue.bstrVal);
	return S_OK;
}


HRESULT CVirtualDevice::OnOpenChanged(IPremiseObject *pObject, VARIANT varNewValue)
{
	HRESULT hr = S_OK;
	ATLTRACE2("[%S]CVirtualDevice::OnOpenChanged(0x%p, %d)\n", m_bstrCommandPrefix, pObject, varNewValue.boolVal);
	
	ObjectLock lock(this); //protect this object from reentrancy
	_ASSERTE(m_spSite.IsEqualObject(pObject));

	hr = ClosePort();
	_ASSERT(SUCCEEDED(hr));
		
	if(varNewValue.boolVal != 0)
	{
		hr = OpenPort(NULL);
		// this is normal, some other app is using a serial port
		if(FAILED(hr))
			pObject->SetValueEx(SVCC_NOTIFY|SVCC_DRIVER, L"Open", &CComVariant(false));
	}
	return S_OK;
}


//
// IPremisePort::Write
//
HRESULT CVirtualDevice::Write(const void *pv, ULONG cb, ULONG *pcbWritten)
{
	HRESULT hr = S_OK;
	CComQIPtr<IPremisePort>		spPremisePort;
	CComQIPtr<IPremiseObject>	spProcessor;

	ATLTRACE2("[%S]CVirtualDevice::Write\n", m_bstrCommandPrefix);
	ObjectLock lock(this);

	CComVariant var;

	// Get the parent (CrestonProcessor)
	if (m_spSite)
		hr = m_spSite->get_Parent(&spProcessor);
	else
		return E_FAIL;

	_ASSERTE(spProcessor);
	if (FAILED(hr)) 
		return hr;

	// Get the Port
	CComQIPtr<IPremiseObject>	pPort;
	FAILED_ASSERT_RETURN_HR(spProcessor->GetValue(L"Network", &var));
	pPort = var.punkVal;
	if(pPort != NULL)
	{
		//get direct port object
		CComVariant var;

		//ATLTRACE2("   Getting _PortInstance\n");
		HRESULT hr = pPort->GetValue(L"_PortInstance", &var);
		//_ASSERTE(SUCCEEDED(hr)); -- commented out because we may try this before the serial ports
		//are actually initialized.  We watch changes on _PortInstance, so we are OK anyways
		spPremisePort = var.punkVal;
		if (spPremisePort == NULL)
		{
			//ATLTRACE2("   spPremisePort == NULL\n");
			return S_FALSE;
		}
		long bOpen = 0;
		//ATLTRACE2("   Getting OpenStatus\n");
		spPremisePort->GetOpenStatus(&bOpen);
		if (bOpen)
		{
			// prefix + ":" + data + "\r\n" + "\0"
			long cbTotal = m_bstrCommandPrefix.Length() + 1 + cb + 2 + 1;
			char* pszWrite = (char*)calloc(cbTotal+1, sizeof(char)); // calloc fills with nulls
			_ASSERTE(pszWrite);
			if (pszWrite == NULL)
				return E_FAIL;

			// Write prefix to COM port
			if(pcbWritten != NULL)
				*pcbWritten = 0;

			if (m_bstrCommandPrefix.Length() > 0)
				sprintf(pszWrite, "%S:", m_bstrCommandPrefix);

			// Copy pv into pszWrite ensuring there is a CRLF at the end
			strncpy(pszWrite+lstrlen(pszWrite), (char*)pv, cb);
			*(pszWrite+cbTotal-3) = '\r';
			*(pszWrite+cbTotal-2) = '\n';

			// Write the data to the COM port
			ATLTRACE2("Writing %d bytes: %s\n", cbTotal, pszWrite);
			hr = spPremisePort->Write(pszWrite, cbTotal-1, pcbWritten);
			if (FAILED(hr))
			{
				free(pszWrite);
				return hr;
			}

			strncpy(pszWrite, (char*)pv, cb);
			*(pszWrite+cb) = '\0';
			m_spSite->SetValueEx(SVCC_NOTIFY|SVCC_DRIVER, L"LastCommandSent", &CComVariant(pszWrite));


//			if(pcbWritten != NULL)
//				*pcbWritten = cb;
			free(pszWrite);
		}
	}
	// TODO: Implement NotifyClients?
	//NotifyClients(L"OnWrite", (const BYTE*)pv, cb);
	return S_OK;
}

//
// IPremisePort::OpenPort
//
// Don't really need to do much...
//
HRESULT CVirtualDevice::OpenPort(IPremisePortCallback* pc)
{
	ATLTRACE2("[%S]CVirtualDevice::OpenPort\n", m_bstrCommandPrefix);
	if(!pc)
		return E_INVALIDARG;

	ClosePort();

	ObjectLock lock(this);
	m_spCB = pc;
	m_bOpen = true;
	//ATLTRACE2("   m_pbOpen = TRUE\n");

	m_spSite->SetValueEx(SVCC_NOTIFY|SVCC_DRIVER, L"Open", &CComVariant(m_bOpen));
	m_spSite->SetValueEx(SVCC_NOTIFY|SVCC_DRIVER, L"LastCommandSent", &CComVariant(L""));
	m_spSite->SetValueEx(SVCC_NOTIFY|SVCC_DRIVER, L"LastCommandReceived",  &CComVariant(L""));

	return S_OK;
}

//
// IPremisePort::ClosePort
//
HRESULT CVirtualDevice::ClosePort()
{
	ATLTRACE2("[%S]CVirtualDevice::ClosePort\n", m_bstrCommandPrefix);
	ClosePortEx(true);
	return S_OK;
}

//
// IPremisePort::ClosePortEx
//
HRESULT CVirtualDevice::ClosePortEx(boolean bImmediate)
{
	ATLTRACE2("[%S]CVirtualDevice::ClosePortEx\n", m_bstrCommandPrefix);
	m_bOpen = FALSE;
	m_spSite->SetValueEx(SVCC_NOTIFY|SVCC_DRIVER, L"Open", &CComVariant(false));
	return S_OK;
}

//
// IPremisePort::GetOpenStatus
//
HRESULT CVirtualDevice::GetOpenStatus(long* pl)
{
	//ATLTRACE2("CVirtualDevice::GetOpenStatus\n");
	*pl = m_bOpen; //always return true
	return S_OK;
}

//
// IPremisePort::GetHandle
//
HRESULT CVirtualDevice::GetHandle(long* pid)//gets a number which can uniquely identify this port
{
	//ATLTRACE2("CVirtualDevice::GetHandle\n");
	return S_OK;
}

//
// IPremisePort::GetDescription
//
HRESULT CVirtualDevice::GetDescription(BSTR* pbstrDesc)
{
	//ATLTRACE2("CVirtualDevice::GetDescription\n");
	if (pbstrDesc == NULL)
		return E_POINTER;

	// HACK: Not sure if this is the right "description". 
	m_bstrFile.CopyTo(pbstrDesc);
	return S_OK;
}

//
// IPremisePort::SetCookie
//
HRESULT CVirtualDevice::SetCookie(DWORD dwCookie)
{
	//ATLTRACE2("CVirtualDevice::SetCookie\n");
	ObjectLock lock(this);
	m_dwCookie = dwCookie;
	return S_OK;
}

//
// IPremisePort::GetCookie
//
HRESULT CVirtualDevice::GetCookie(DWORD* pdwCookie)
{
	//ATLTRACE2("CVirtualDevice::GetCookie\n");
	if (pdwCookie == NULL)
		return E_POINTER;
	ObjectLock lock(this);
	*pdwCookie = m_dwCookie;
	return S_OK;
}

//
// IPremisePort::GetCallback
//
HRESULT CVirtualDevice::GetCallback(IPremisePortCallback** pCallback)
{
	//ATLTRACE2("CVirtualDevice::GetCallback\n");
	ObjectLock lock(this);
	return m_spCB.CopyTo(pCallback);
}

//
// IPremisePort::SetCallback
//
HRESULT CVirtualDevice::SetCallback(IPremisePortCallback* pc)
{
	//ATLTRACE2("CVirtualDevice::SetCallback\n");
	ObjectLock lock(this);
	m_spCB = pc;
	return S_OK;
}

//
// IPremiseHandlePort::SetupPort
//
HRESULT CVirtualDevice::SetupPort(LPCOLESTR pszFile, DWORD dwDesiredAccess, DWORD dwShareMode, 
		DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes)
{
	//ATLTRACE2("CVirtualDevice::SetupPort\n");
	ObjectLock lock(this);
	m_bstrFile = pszFile;
	return S_OK;
}

//
// IPremiseHandlePort::SetPortSpy
//
HRESULT CVirtualDevice::SetPortSpy(IUnknown* pSite)
{
	//ATLTRACE2("CVirtualDevice::SetPortSpy\n");
	ObjectLock lock(this);
	pSite->QueryInterface(&m_spSite);
	return S_OK;
}



HRESULT CVirtualDevice::OnBrokerDetach()
{
	HRESULT     hr = 0;
    
	ATLTRACE2("[%S]CVirtualDevice::OnBrokerDetach\n", m_bstrCommandPrefix);
	hr = ClosePort();
	_ASSERT(SUCCEEDED(hr));
	m_spSite->SetValueEx(SVCC_LOCAL|SVCC_NOTIFY, L"_PortInstance", &CComVariant((IUnknown*)NULL));

	return S_OK;
}

HRESULT CVirtualDevice::OnBrokerAttach()
{
	// this allows the driver to get to the IPremisePort
	m_spSite->SetValueEx(SVCC_LOCAL|SVCC_NOTIFY, L"_PortInstance", &CComVariant(GetUnknown()));

	CComVariant var;
	m_spSite->GetValue(L"CommandPrefix", &var);
	if (var.vt == VT_BSTR)
		m_bstrCommandPrefix = var.bstrVal;

	ATLTRACE2("[%S]CVirtualDevice::OnBrokerAttach\n", m_bstrCommandPrefix);

	return S_OK;
}

