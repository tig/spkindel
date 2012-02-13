// ExternalProcessor.cpp : Implementation of CExternalProcessor

#include "stdafx.h"
#include "ExternalProcessor.h"


/////////////////////////////////////////////////////////////////////////////
// CExternalProcessor
HRESULT CExternalProcessor::OnObjectCreated(IPremiseObject* pContainer, IPremiseObject* pCreatedObject)
{
	if (pContainer == m_spSite)
		return AddVirutalDevice(pCreatedObject, true);
	return S_OK;
}

// Maintain list of controllers associated with this driver
HRESULT CExternalProcessor::OnObjectDeleted(IPremiseObject *pContainer, IPremiseObject *pDeletedObject)
{
	if (pContainer == m_spSite)
		return RemoveVirutalDevice(pDeletedObject);
	return S_OK;
}

HRESULT CExternalProcessor::AddVirutalDevice(IPremiseObject* pSite, bool bFirstTime)
{
	CComPtr<IObjectWithSite> spOWS;
	CComPtr<IPremisePortCallback> spCB;
	HRESULT hr = S_OK;
	CComBSTR	bstr;

	pSite->get_Name(&bstr);
	FAILED_ASSERT_RETURN_HR(CVirtualDevice::CreateInstance(&spOWS));
	FAILED_ASSERT_RETURN_HR(spOWS->SetSite(pSite));
	
	return m_vecVirtualDevices.Add(SPIOWS(spOWS)) ? S_OK : E_FAIL;
}

HRESULT CExternalProcessor::RemoveVirutalDevice(IPremiseObject* pSite)
{
	int i;
	for (i=0;i<m_vecVirtualDevices.GetSize();i++)
	{
		CComPtr<IPremiseObject> spSite;
		IObjectWithSite* pOWS = m_vecVirtualDevices[i].m_T;
		pOWS->GetSite(IID_IPremiseObject, (void**)&spSite);
		if (pSite == spSite)
		{
			pOWS->SetSite(NULL);
			m_vecVirtualDevices.RemoveAt(i);
			return S_OK;
		}
	}
	return E_FAIL;
}

HRESULT CExternalProcessor::OnBrokerAttach()
{
	/*
	SUBFOLDER arrProc[] = 
	{
		{1, SVCC_FIXED | SVCC_EXIST | SVCC_NOTIFY | SVCC_DRIVER, XML_Device_VirtualDevice, L"VirtualDevice", false, 0, NULL, 0},
	};
	m_spSite->TransactionOpen(SVCC_NOTIFY, SVT_CREATE);
	HRESULT hr = CreateSubFolderTree(m_spSite, arrProc, 1);
	m_spSite->TransactionCommit();
	*/

	CComVariant var;
	m_spSite->GetValue(L"WatchdogCommand", &var);
	if (var.vt == VT_BSTR)
	{
		m_bstrWatchdogCommand = var.bstrVal;
		if (m_bstrWatchdogCommand.Length())
			m_bstrWatchdogCommand.Append(L"\r\n");
	}
	else
		m_bstrWatchdogCommand = L"";

	//loop through all objects and set up controllers
	CComPtr<IUnknown> spEnumUnk;
	HRESULT hr = m_spSite->_NewEnum(&spEnumUnk);
	if (FAILED(hr))
		return hr;
	
	CComPtr<IEnumVARIANT> spEnum;
	hr = spEnumUnk->QueryInterface(&spEnum);
	if (FAILED(hr))
		return hr;
	
	CComVariant varElem;
	for (  ;spEnum->Next(1, &varElem, NULL) == S_OK; varElem.Clear())
	{
		CComQIPtr<IPremiseObject> spElem(varElem.pdispVal);
		hr = AddVirutalDevice(spElem, false);
	}

	return S_OK;
}


HRESULT CExternalProcessor::OnBrokerDetach()
{
	// clean up all old objects
	int i;
	for (i=0;i<m_vecVirtualDevices.GetSize();i++)
		m_vecVirtualDevices[i].m_T->SetSite(NULL);
	m_vecVirtualDevices.RemoveAll();

	return S_OK;
}

HRESULT CExternalProcessor::OnConfigurePort(IPremiseObject* pPort)
{
	// TODO: Allow settings to be set by user

	//Configure serial port for specific device
	pPort->SetValue(L"Baud", &CComVariant(SERIAL_9600)); //9600 baud
	pPort->SetValue(L"DataBits", &CComVariant(SERIAL_DATABITS_8));
	pPort->SetValue(L"Parity", &CComVariant(SERIAL_PARITY_NONE));
	pPort->SetValue(L"StopBits", &CComVariant(SERIAL_STOPBITS_1));
	pPort->SetValue(L"FlowControl", &CComVariant(SERIAL_FLOWCONTROL_NONE));
	pPort->SetValue(L"RTS", &CComVariant(SERIAL_RTS_ENABLE));
	pPort->SetValue(L"DTR", &CComVariant(SERIAL_DTR_ENABLE));
	pPort->SetValue(L"CTS", &CComVariant(false));
	pPort->SetValue(L"DSR", &CComVariant(false));
	return S_OK;
}

bool CExternalProcessor::OnWatchDog()
{
	if (m_bstrWatchdogCommand.Length() > 0)
		SendBufferedCommand(CW2A(m_bstrWatchdogCommand));

	return true;
}

// ProcessLine 
void CExternalProcessor::ProcessLine(LPCSTR psz)
{
	size_t  pos;
	int		cb;
	LPCSTR pszData = NULL;
	char	szCommandPrefix[255];

	//ATLTRACE2("Processline(%s)\n", psz);

	SetAckReceived();	// The only Ack we really require is for the watchdog

	// Less than 3 chars (e.g. "a:b") then it is invalid
	cb = (int)strlen(psz);
	if (cb < 3)
		return ;

	pos = strcspn(psz, ":");
	if (pos != 0)
	{
		CComQIPtr<IPremisePort>	spVD;
		CComPtr<IPremiseObject>	spO;
		CComPtr<IPremisePortCallback>	spCallback;
		CComVariant varValue(szCommandPrefix);

		strncpy(szCommandPrefix, psz, pos);
		szCommandPrefix[pos] = '\0';
		pszData = psz+pos+1;

		// Get the child VirutalDevice with szCommandPrefix
		//
		HRESULT hr = S_OK;
        varValue = szCommandPrefix;

		_ASSERTE(m_spSite);
		hr = m_spSite->GetChildByProperty(L"CommandPrefix", varValue, &spO);
		if (SUCCEEDED(hr))
		{
			// Get the IPremisePort via the _PortInstance property
			HRESULT hr = spO->GetValue(L"_PortInstance", &varValue);
			if (FAILED(hr))
			{
				//ATLTRACE2("  Get _PortInstance on VirutalDevice site failed\n");
				return;
			}
			
			spVD = varValue.punkVal;
			_ASSERTE(spVD);
			if (spVD != NULL)
			{
				hr = spVD->GetCallback(&spCallback);
				if (SUCCEEDED(hr))
				{
					//ATLTRACE2("  Calling spCallback->OnNewData\n");
					spCallback->OnNewData((const byte*)pszData, (ULONG)strlen(pszData), (IPremisePort*)0xbaad);
					spCallback->OnNewData((const byte*)"\r\n", (ULONG)2, (IPremisePort*)0xbaad);

					varValue.Clear();
					varValue = pszData;
					spO->SetValueEx(SVCC_NOTIFY|SVCC_DRIVER, L"LastCommandReceived", &varValue);
				}
			}
		}
	}
	else
		return;

	return;

}

HRESULT CExternalProcessor::OnDeviceState(DEVICE_STATE ps)
{
	switch (ps)
	{
		case DEVICE_INIT:
			//ATLTRACE2("CrestonProcessor::OnDeviceState(DEVICE_INIT)\n");
			//Add any code needed to initialize device here
			CComVariant var;
			m_spSite->GetValue(L"WatchdogCommand", &var);
			if (var.vt == VT_BSTR)
			{
				m_bstrWatchdogCommand = var.bstrVal;
				if (m_bstrWatchdogCommand.Length())
					m_bstrWatchdogCommand.Append(L"\r\n");
				SendBufferedCommand(CW2A(m_bstrWatchdogCommand));
			}
			else
				m_bstrWatchdogCommand = L"";

		break;
	}
	return S_OK;
}


HRESULT CExternalProcessor::OnWatchdogCommandChanged(IPremiseObject *pObject, VARIANT newValue)
{
	//ATLTRACE2("CExternalProcessor::OnWatchdogCommandChanged\n");
	
	m_bstrWatchdogCommand = newValue.bstrVal;
	if (m_bstrWatchdogCommand.Length())
		m_bstrWatchdogCommand.Append(L"\r\n");

	return S_OK;
}
