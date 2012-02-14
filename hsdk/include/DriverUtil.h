//***************************************************************************
//  SYS
//  Copyright (C) Premise Systems, Inc. 1998-2001.
//

#ifndef __DRIVERUTIL_89762323864_H_
#define __DRIVERUTIL_89762323864_H_

#include <systrace.h>
#ifdef _INTDRIVER
#include <prbroker.h>
#else
#include <syspub.h>
#endif
#include <bytebuffer.h>
#include <prcomm.h>
#include <hex.h>
#include <SysLock.h>
#include <SysTime.h>
#include <SysLogger.h>
#include <SysMacros.h>
#include <SysJump.h>
#include <SysSerial.h>

static const DWORD s_dwCmdAck = 10000; //wait 10 seconds for command ack
static const DWORD s_dwPingFreq = 60000; //ping every 60 seconds
static const DWORD s_dwRecoveryCycle = 300000; //5 minutes
static const DWORD s_dwHeartbeat = 2000; //OnHeartbeatTimer called every 2 seconds

enum COMMAND_ACK
{
	CACK_EXPLICIT,	//explicit from driver
	CACK_TIMER,		//ack on timer
	CACK_WRITE,		//ack on write completed
	CACK_TIMEOUT,	//explicit ack with timeout callback
	CACK_NONE
};

enum DEVICE_STATE
{
	PORT_OPENING=0,
	PORT_OPENED,
	PORT_OPEN_FAILED,
	PORT_OPEN_INUSE,
	DEVICE_INIT,
	PORT_CLOSING,
	PORT_CLOSED,
};

__declspec(selectany) OLECHAR* szDEVICE_STATE[] = 
{
	L"Port(Opening)",
	L"Port(Opened)",
	L"Port(OpenFailed)",
	L"Port(AlreadyInUse)",
	NULL,
	L"Port(Closing)",
	L"Port(Closed)"
};

enum WRITE_STATUS
{
	WRITE_NONE,
	WRITE_PENDING,
	WRITE_SUCCEEDED,
	WRITE_FAILED
};

enum RECOMODE
{
	RECOMODE_NONE,			//normal operating mode
							//new commands go to m_commands
	RECOMODE_INIT,			//driver is sending reinit commands
							//new commands go to m_commands
	RECOMODE_INITPENDING,	//waiting for init to finish
							//new commands go to m_commandsBackup
};

inline bool IsObjectOfExplicitType(IPremiseObject* pObject, BSTR bstrClassName)
{
	VARIANT_BOOL bType = VARIANT_FALSE;
	HRESULT hr = pObject->IsOfExplicitType(bstrClassName, &bType);
	if(SUCCEEDED(hr) && bType)
		return true;
	return false;
}

struct SUBFOLDER
{
	int nCount;  //how many to create of this
	long flags; //CreateEx flags
	LPWSTR lpszSchema; //schema - sys://Schema/Lutron/HomeWorks/Relays
	LPWSTR lpszName; //name of node - Relays
	bool bAppendNumber; //whether to append number to end of node name
	int nStart; // starting number when appending number
	LPWSTR lpszProperty; //property to stuff number into
	int nStartProp; //starting number for property value
};

/* example
SUBFOLDER arrLights[] = 
{
{1, SVCC_FIXED | SVCC_EXIST | SVCC_NOTIFY, XML_HomeWorks_Lights, L"Lights", false, 0, NULL, 0},
{48, SVCC_FIXED | SVCC_EXIST | SVCC_NOTIFY, XML_HomeWorks_Dimmer, L"Light", true, 1, L"DeviceNumber", 1}
};
HRESULT hr = CreateSubFolderTree(pCreatedObject, arrLights, 2);
*/

inline HRESULT CreateSubFolderTree(IPremiseObject* base, const SUBFOLDER* subfolders, int nLen)
{
	if (nLen == 0)
		return S_OK;
	
	const SUBFOLDER& sf = subfolders[0];
	
	USES_CONVERSION;
	HRESULT hr=S_OK;
	
	for (int i=0;i<sf.nCount;i++)
	{
		LPWSTR pszwName = sf.lpszName;
		TCHAR baseName[512];
		if (sf.bAppendNumber)
		{
			wsprintf(baseName, _T("%s%u"), W2T(sf.lpszName), i+sf.nStart);
			pszwName = T2W(baseName);
		}
		CComPtr<IPremiseObject> spNew;
		hr = base->CreateEx(sf.flags, sf.lpszSchema, pszwName, &spNew);
		if (FAILED(hr))
			break;
		if (sf.lpszProperty)
			spNew->SetValueEx(SVCC_DRIVER | SVCC_NOTIFY, sf.lpszProperty, &CComVariant(sf.nStartProp+i));
		hr = CreateSubFolderTree(spNew, &subfolders[1], nLen-1);
		if (FAILED(hr))
			break;
	}
	return hr;
}

class _command
{
public:
	_command()
	{
		ZeroMemory(this, sizeof(_command));
	}
	_command(const BYTE* p, ULONG len, UINT_PTR n, COMMAND_ACK ack, DWORD dwAA)
	{
		init(p, len, n, ack, dwAA);
	}
	_command(const _command& c)
	{
		memcpy(this, &c, sizeof(c));
		_command* pc= (_command*)&c;
		ZeroMemory(pc, sizeof(c));
	}
	~_command()
	{
		delete [] pstr;
	}
	void init(const BYTE* p, ULONG len, UINT_PTR n, COMMAND_ACK ack, DWORD dwAA)
	{
		nLen = len;
		pstr = new BYTE[nLen];
		memcpy(pstr, p, nLen*sizeof(BYTE));
		nCmdID = n;
		ackType = ack;
		dwAutoAck = dwAA;
		dwRetries = 0;
		bCmdRcvd = false;
		bSent = false;
	}
	_command& operator=(const _command& c)
	{
		delete [] pstr;
		memcpy(this, &c, sizeof(c));
		_command* pc= (_command*)&c;
		ZeroMemory(pc, sizeof(c));
		return *this;
	}
	BYTE*		pstr;
	ULONG		nLen;
	UINT_PTR	nCmdID;
	COMMAND_ACK ackType;
	bool		bSent;   	//has this command been sent?
	DWORD		dwAutoAck;
	DWORD		dwRetries;	// used for bus topologies (RS-485)
	bool		bCmdRcvd;	// used for bus topologies
};

class ATL_NO_VTABLE IPremiseNotifyImpl : public IPremiseNotify
{
public:
	// overridable functions
	// IPremiseNotify
	
	// interface functions ---
	
	STDMETHOD(OnValueChanged)(long subscriptionID, long transactionID, long propagationID, long 
		controlCode, IPremiseObject *pObject, BSTR bstrPropertyName, VARIANT newValue, VARIANT oldValue)
	{
		if (controlCode & (SVCC_DRIVER | SVCC_VOLATILE))	// ignore driver initiated or non-committed notifications except reference properties
			return S_OK;
		
		return OnObjectValueChanged(pObject, bstrPropertyName, newValue);
	}
	
	
	STDMETHOD(OnObjectCreated)(long subscriptionID, long transactionID, long propagationID, long 
		controlCode, IPremiseObject *pObjectCreated, IPremiseObject *pContainer, IPremiseObject *pInsertBefore)
	{
		//ATLTRACE2("IPremiseNotifyImpl::OnObjectCreated(%d, %d, %d, %d, 0x%p, 0x%p, 0x%p)\n", 
		//	subscriptionID, transactionID, propagationID, controlCode, pObjectCreated, pContainer, pInsertBefore);
		if (controlCode & SVCC_PASTE) // ignore paste?
			return S_OK;
		return OnObjectCreated(pContainer, pObjectCreated);
	}
	
	
	STDMETHOD(OnObjectDeleted)(long subscriptionID, long transactionID, long propagationID, long 
		controlCode, IPremiseObject *pObjectDeleted, IPremiseObject *pContainer)
	{
		return OnObjectDeleted(pContainer, pObjectDeleted);
	}
	
	STDMETHOD(OnPreObjectDeleted)(long subscriptionID, long transactionID, long propagationID, long 
		controlCode, IPremiseObject *pObjectDeleted, IPremiseObject *pContainer)
	{
		return OnPreObjectDeleted(pContainer, pObjectDeleted);
	}
	
	
	STDMETHOD(OnTransactionComplete)(long subscriptionID,
		long transactionID, long propagationID, long controlCode)
	{
		return E_NOTIMPL;
	}
	
	STDMETHOD(OnPropertyChanged)(long subscriptionID, long transactionID, long propagationID, long 
		controlCode, IPremiseObject* pObject, IPremiseObject* pProperty, VARIANT* pNewValue, VARIANT* pOldValue)
	{
		//USE THIS IN ALL NEW IMPLES
		
		// new: for higher performance...
		// OnValueChanged will also be called for any property.
		return E_NOTIMPL;
	}
	
	STDMETHOD(OnSchemaChanged)(long subscriptionID, 
		long transactionID, long propagationID, long controlCode, 
		IPremiseObject* pObjectInstance, IPremiseObject* pObjectClass)
	{
		return E_NOTIMPL;
	}
	
	
	// simplified functions ---
	
	STDMETHOD(OnObjectCreated)(IPremiseObject *pContainer, IPremiseObject *pCreatedObject)
	{
		// called when an object is created
		return E_NOTIMPL;
	}
	
	
	STDMETHOD(OnObjectDeleted)(IPremiseObject *pContainer, IPremiseObject *pDeletedObject)
	{
		// called when a child object is deleted
		return E_NOTIMPL;
	}
	
	STDMETHOD(OnPreObjectDeleted)(IPremiseObject *pContainer, IPremiseObject *pDeletedObject)
	{
		// called when an object itself is deleted
		return E_NOTIMPL;
	}
	
	
	STDMETHOD(OnObjectValueChanged)(IPremiseObject *pObject, BSTR bstrProperty, const VARIANT &
		varNewValue)
	{
		// called when a value changes on an object
		return E_NOTIMPL;
	}	
};

// macros to use with IPremiseNotifyImpl

#define BEGIN_NOTIFY_MAP( theClass ) \
	virtual HRESULT STDMETHODCALLTYPE OnObjectValueChanged(IPremiseObject* pObject, BSTR bstrProperty, const VARIANT& varNewValue) \
{

#define NOTIFY_PROPERTY( theProperty, theFunction ) \
	if(lstrcmpW(bstrProperty, theProperty)==0) \
{ \
	(theFunction)(pObject, varNewValue); \
}

// functions must be defined as:
// virtual HRESULT STDMETHODCALLTYPE OnChangedXXXX(IPremiseObject* pObject, VARIANT newValue);
#define END_NOTIFY_MAP() return S_OK;}

class __declspec(novtable) CPremiseSubscriber : 
	public IObjectWithSite, 
	public IPremiseNotifyImpl
{
public:
	CComPtr<IPremiseObject>     m_spSite;
	long                        m_lSubscriptionID;
public: //Overrides
	virtual HRESULT OnBrokerAttach() 
	{
		//ATLTRACE2("CPremiseSubscriber::OnBrokerAttach\n");
		return S_OK;
	}
	virtual HRESULT OnBrokerDetach()
	{
		return S_OK;
	}
public:
	CPremiseSubscriber()
	{
		m_lSubscriptionID = 0;
	}
	~CPremiseSubscriber()
	{
		_ASSERTE((m_spSite==NULL)&& "SetSite(NULL) not called before object destroyed.");
	}
	
// IObjectWithSite	
	STDMETHOD(SetSite)(IUnknown *pSite)
	{
		HRESULT hr = 0;

		//ATLTRACE2("CPremiseSubscriber::SetSite(0x%p)\n", pSite);
		
		// subscribe to notifications
		// clean up
		CComPtr<IPremiseObject> spSite(m_spSite);
		if(spSite)
		{
			CComBSTR	bstr;
			spSite->get_Name(&bstr);
			//ATLTRACE2("  m_spSite is %S\n", bstr);
			//let derived class cleanup
			OnBrokerDetach();
			// unsubscribe from properties
			spSite->UnsubscribeFromProperty(m_lSubscriptionID);
			m_lSubscriptionID = 0;
			
			// clean up
			m_spSite.Release();
		}
		
		if(pSite != NULL)
		{
			// initialize - m_spSite 
			hr = pSite->QueryInterface(&m_spSite);
			_ASSERTE(SUCCEEDED(hr));
			if(FAILED(hr))
				return hr;
			
			hr = m_spSite->SubscribeToProperty(NULL, (IObjectWithSite*)this, &m_lSubscriptionID);
			_ASSERTE(SUCCEEDED(hr));
			if (FAILED(hr))
				return hr;
			//ATLTRACE2("  Calling OnBrokerAttach()\n"); 
			OnBrokerAttach();
		}

		return S_OK;
	}	
	STDMETHOD(GetSite)(REFIID riid, void **ppSite)
	{
		if(!m_spSite)
			return E_FAIL;
		return m_spSite->QueryInterface(riid, ppSite);
	}
};


class CPremisePortDevice
{
public:
	virtual HRESULT OnPortInstanceChanged(IPremiseObject *pObject, VARIANT newValue) = 0;
};

//this class is used for subscribing to the _PortInstance property on a port object
//when it changes we notify the 
class CPortInstanceSubscriber : 
	public CComObjectRootEx<CComMultiThreadModel>,
	public CPremiseSubscriber
{
public:
	CPortInstanceSubscriber()
	{
		m_pCB = NULL;
	}
BEGIN_NOTIFY_MAP(CPortInstanceSubscriber)
	NOTIFY_PROPERTY(L"_PortInstance", OnPortInstanceChanged) 
END_NOTIFY_MAP() 
BEGIN_COM_MAP(CPortInstanceSubscriber)
	COM_INTERFACE_ENTRY(IObjectWithSite)
	COM_INTERFACE_ENTRY(IPremiseNotify)
END_COM_MAP()

	void SetCallback(CPremisePortDevice* p)
	{
		m_pCB = p;
	}

	HRESULT STDMETHODCALLTYPE OnPortInstanceChanged(IPremiseObject *pObject, VARIANT newValue)
	{
		//ATLTRACE2("CPortInstanceSubscriber::OnPortInstanceChanged(0x%p, 0x%p)\n", pObject, newValue.pdispVal);
		if (m_pCB != NULL)
			m_pCB->OnPortInstanceChanged(pObject, newValue);
		return S_OK;
	}

// this is a weak ref to avoid circular reference count
	CPremisePortDevice* m_pCB;
};


class __declspec(novtable) CPremiseBufferedPortDeviceBase : 
	public CPremisePortDevice,
	public CPremiseSubscriber,
	public IPremisePortCallback,
	public CLogger
{
protected:
	CComAutoCriticalSection m_csBPD;
	PROTECT(m_csBPD, CComObject<CPortInstanceSubscriber>*, m_pPIS);
	PROTECT(m_csBPD, CComQIPtr<IPremisePort>, m_spPremisePort);
	PROTECT(m_csBPD, bool, m_bPortOpened);
	PROTECT(m_csBPD, CComBSTR, m_bstrPortName);
	PROTECTARRT(m_csBPD, BYTE, CSimpleArray<BYTE>, m_arrTerms);
	//
	PROTECT(m_csBPD, bool, m_bCanSendCommand);
	PROTECTARRT(m_csBPD, _command, CSimpleArray<_command>, m_commands);
	PROTECT(m_csBPD, UINT_PTR, m_nLastCmdIDSent);

	//watchdog related stuff
	PROTECTARRT(m_csBPD, _command, CSimpleArray<_command>, m_commandsBackup);
	CPremiseTimer<CPremiseBufferedPortDeviceBase> m_timerHeartbeat;
	CPremiseTimer<CPremiseBufferedPortDeviceBase> m_timerAck;
	PROTECT(m_csBPD, COMMAND_ACK, m_ackType);
	PROTECT(m_csBPD, RECOMODE, m_RecoveryMode);
	PROTECT(m_csBPD, DWORD, m_dwLastCommandSentTick);
	PROTECT(m_csBPD, DWORD, m_dwLastPing);
	PROTECT(m_csBPD, DWORD, m_dwLastRecovery);
	PROTECT(m_csBPD, int, m_nRecovery);

	//input stuff
	//m_buffer doesn't need critical section because it is only used in OnNewData and our
	//architecture only allows one IO thread at a time.  We don't issue a new read until
	//we return from processing the current one.
	bytebuffer<256> m_buffer;
	bool m_bClearBuffer;

public:
	//Required overrides by final driver class
	virtual HRESULT OnDeviceState(DEVICE_STATE ps) = 0;
	virtual bool OnPing() = 0;
#define OnWatchDog OnPing	//OnWatchDog is deprecated, use OnPing

	//Optional overrides
	//OnRecovery is called when recovery is about to take place
	virtual void OnRecovery()
	{
	}
	//Implement OnHeartBeat if you want a callback whenever the heartbeat timer fires (~2seconds)
	virtual void OnHeartbeat()
	{
	}
	//Override to be notified when a buffered write is about to occur
	//This can be useful if you need to know when the command is actually going out the port
	//as data is normally buffered
	virtual void OnBufferedWrite(_command& cmd)
	{
	}
	//Implement ProcessLine if you want to see data in line chunks, ideal for text mode drivers
	//SetTextModeTerminators can be used to change the default set of 0x0A, 0x0D, and 0x00
	virtual void ProcessLine(LPCSTR psz)
	{
		_ASSERTE(false && "Need to override ProcessLine, ProcessReadBuffer, or OnNewData");
	}
	//called when CACK_TIMEOUT is specified (SendBufferedDataWithTimeout)
	//override OnAckTimeout if you want to do something special when a time out occurs
	//returning false prevents the default automatic Ack from  happening
	virtual bool OnAckTimeout()
	{
		return true;
	}

	//called for handling property changes
	virtual HRESULT OnPortInstanceChanged(IPremiseObject *pObject, VARIANT newValue)
	{
		//ATLTRACE2("CPremiseBufferedPortDeviceBase::OnPortInstanceChanged(0x%p, 0x%p)\n", pObject, newValue.pdispVal);
		CComQIPtr<IPremisePort> spPort = newValue.pdispVal;
		return SetPortInstance(spPort, true);
	}
	HRESULT STDMETHODCALLTYPE OnLoggingChanged(IPremiseObject *pObject, VARIANT newValue)
	{
		CSLock cslock(&m_csBPD);		
		if (newValue.boolVal == VARIANT_FALSE)
			StopLogging();
		else
		{
			CComBSTR bstrName;
			pObject->get_Name(&bstrName);
			StartLogging(bstrName);
		}
		return S_OK;
	}

	/*
		The SendXXX functions are used for sending data to the device
	*/
	//data sent through SendImmediateData is not buffered
	//and will not be preserved if the device is reset
	HRESULT SendImmediateData(BYTE* p, int nLen, UINT_PTR nCommandID = 0)
	{
		CSLock lock(&m_csBPD);
		if (!CheckPort())
			return S_FALSE;
		DWORD dwWritten;
		m_nLastCmdIDSent = nCommandID;
		WriteDataToLog(p, nLen, LoggerToDevice);
		return m_spPremisePort->Write(p, nLen, &dwWritten);
	}
	HRESULT SendImmediateCommand(LPCSTR p, UINT_PTR nCommandID = 0)
	{
		return SendImmediateData((BYTE*)p, lstrlenA(p), nCommandID);
	}
	//as soon as this write completes the next chunk of data will go out
	HRESULT SendBufferedDataNoAck(BYTE* p, int nLen, UINT_PTR nCommandID = 0)
	{
		return SendData(p, nLen, CACK_WRITE, s_dwCmdAck, nCommandID);
	}
	//When SetAckReceived is called by the driver, the next chunk can go out
	HRESULT SendBufferedData(BYTE* p, int nLen, UINT_PTR nCommandID = 0)
	{
		return SendData(p, nLen, CACK_EXPLICIT, s_dwCmdAck, nCommandID);
	}
	//Wait the specified time before sending next chunk of data out
	HRESULT SendBufferedDataAndWait(BYTE* p, int nLen, DWORD dwWait, UINT_PTR nCommandID = 0)
	{
		return SendData(p, nLen, CACK_TIMER, dwWait, nCommandID);
	}
	//Wait for either an explicit ack (SetAckReceived) or a timeout to occur before sending next chunk of data
	HRESULT SendBufferedDataWithTimeout(BYTE* p, int nLen, DWORD dwWait, UINT_PTR nCommandID = 0)
	{
		return SendData(p, nLen, CACK_TIMEOUT, dwWait, nCommandID);
	}
	//The SendBufferedCommandXXXX functions are the same as SendBufferedDataXXXX except they take strings
	HRESULT SendBufferedCommandNoAck(LPCSTR p, UINT_PTR nCommandID = 0)
	{
		return SendData((BYTE*)p, lstrlenA(p), CACK_WRITE, s_dwCmdAck, nCommandID);
	}
	HRESULT SendBufferedCommand(LPCSTR p, UINT_PTR nCommandID = 0)
	{
		return SendData((BYTE*)p, lstrlenA(p), CACK_EXPLICIT, s_dwCmdAck, nCommandID);
	}
	HRESULT SendBufferedCommandAndWait(LPCSTR p, DWORD dwWait, UINT_PTR nCommandID = 0)
	{
		return SendData((BYTE*)p, lstrlenA(p), CACK_TIMER, dwWait, nCommandID);
	}
	HRESULT SendBufferedCommandWithTimeout(LPCSTR p, DWORD dwWait, UINT_PTR nCommandID = 0)
	{
		return SendData((BYTE*)p, lstrlenA(p), CACK_TIMEOUT, dwWait, nCommandID);
	}
	
public:
	CPremiseBufferedPortDeviceBase() : 
		m_timerHeartbeat(this, &CPremiseBufferedPortDeviceBase::OnHeartbeatTimer, 0),
		m_timerAck(this, &CPremiseBufferedPortDeviceBase::OnAckTimer, 0)
	{
		CSLock lock(&m_csBPD);
		HRESULT hr = CComObject<CPortInstanceSubscriber>::CreateInstance(&m_pPIS);
		_ASSERTE(SUCCEEDED(hr));
		m_pPIS->AddRef();
		m_bPortOpened = false;

		m_bCanSendCommand = true;
		m_nLastCmdIDSent = 0;
		m_bClearBuffer = false;
		m_RecoveryMode = RECOMODE_NONE;
		m_dwLastPing = 0;
		m_ackType = CACK_NONE;
		m_dwLastCommandSentTick = 0;
		m_dwLastRecovery = 0;
		m_nRecovery = 0;
		m_arrTerms.Add((BYTE)0x0A);
		m_arrTerms.Add((BYTE)0x0D);
		m_arrTerms.Add((BYTE)0);
	}
	~CPremiseBufferedPortDeviceBase()
	{
		CSLock lock(&m_csBPD);
		ClearBufferedCommands();
		m_pPIS->SetCallback(NULL);
		m_pPIS->SetSite(NULL);
		m_pPIS->Release();
		SetPortInstance(NULL);
	}

//IPremisePortCallback methods
	STDMETHOD(OnNewData)(const BYTE* pv, ULONG cb, IPremisePort* pPort)
	{
		WriteDataToLog(pv, cb, LoggerFromDevice);
		if (m_bClearBuffer)
		{
			m_buffer.nSize = 0;
			m_bClearBuffer = false;
		}
		m_buffer.Append((BYTE*)pv, cb);
		
		unsigned long dw = m_buffer.nSize;
		unsigned long n = ProcessReadBuffer(m_buffer.pb, dw, /*pPort*/(IPremisePort*)0xbaad);
		_ASSERTE(dw == m_buffer.nSize);
		m_buffer.Remove(n);
		return S_OK;
	}
	
	STDMETHOD(OnWriteSucceeded)(const BYTE* pv, ULONG cb, IPremisePort* pPort)
	{
		CSLock lock(&m_csBPD);
		WriteDataToLog(pv, cb, LoggerToDeviceSuccess);
		if (m_commands.GetSize() != 0)
		{
			_command& cmd = m_commands[0];
			if (cmd.bSent && (cmd.ackType == CACK_WRITE))
			{
				//weird case could (and does occasionally) happen
				//where we get notified that a write completed after we've already sent
				//another command
				if ((cmd.nLen == cb) && (memcmp(cmd.pstr, pv, cb)==0))
					SetAckReceived();
			}
		}
		return S_OK;
	}
	STDMETHOD(OnWriteError)(const BYTE* pv, ULONG cb, IPremisePort* pPort)
	{
		WriteDataToLog(pv, cb, LoggerToDeviceFailed);
		return S_OK;
	}
	//only used for IP ports, not for serial
	STDMETHOD(OnNewPort)(IPremisePort* pSourcePort, IPremisePort* pNewPort)
	{
		return S_OK;
	}
	STDMETHOD(OnPortStatus)(PremiseNetStatus lPortStatus, IPremisePort* pPort)
	{
		switch (lPortStatus)
		{
		case PREMISE_NETSTAT_CLOSED:
			SetPortInstance(NULL);
			break;
#ifdef OPEN_SERIAL_ASYNC
		case PREMISE_NETSTAT_OPENED:
			OnDeviceStateImpl(PORT_OPENED);
			m_bPortOpened = true;
			OnDeviceStateImpl(DEVICE_INIT);
			break;
		case PREMISE_NETSTAT_OPEN_FAILED:
			OnDeviceStateImpl(PORT_OPEN_FAILED);
			SetPortInstance(NULL, false);
			break;
#endif
		}
		return S_OK;
	}
	//used for Ring, CTS, etc
	STDMETHOD(OnEvent)(unsigned long nEvent)
	{
		return S_OK;
	}

	//should be overriden only by impl classes
	//this makes it simpler for drivers
	virtual HRESULT OnDeviceStateImpl(DEVICE_STATE ps)
	{
		LPOLESTR psz = szDEVICE_STATE[ps];
		switch(ps)
		{
		case PORT_OPENING:
		case PORT_OPENED:
			break;
		case PORT_OPEN_FAILED:
		case PORT_OPEN_INUSE:
			SetFailure();
			break;
		case PORT_CLOSED:
			ClearBufferedCommands();
			ExitRecoveryMode();
			break;
		case PORT_CLOSING:
		case DEVICE_INIT:
			ClearInputBuffer();
			break;
		}
		if ((m_spSite != NULL) && (psz != NULL))
				m_spSite->SetValue(L"PortStatus", &CComVariant(psz));
		if (IsLogging() && (psz != NULL))
		{
			USES_CONVERSION;
			LPSTR pszA = OLE2A(psz);
			WriteDataToLog((const BYTE*)pszA, lstrlenA(pszA), LoggerInfo, false);
		}
		return OnDeviceState(ps);
	}

	HRESULT SetPortInstance(IPremisePort* p, bool bCloseOldPort = true)
	{
		//ATLTRACE2("CPremisePortDeviceBase::SetPortInstance(0x%p, %d)\n", p, bCloseOldPort);

		CSLock cslock(&m_csBPD);		
		if (m_spPremisePort == p)
		{
			//ATLTRACE2("   m_spPremisePort == p\n");
			return S_OK;
		}
		
		HRESULT hr = S_OK;
		//Close existing port and release everything
		if (m_spPremisePort != NULL)
		{
			//prevent reentrancy
			CComPtr<IPremisePort> spPremisePort;
			spPremisePort.Attach(m_spPremisePort.Detach());

			if (bCloseOldPort)
			{
				OnDeviceStateImpl(PORT_CLOSING);
				spPremisePort->ClosePortEx(1);
				m_bPortOpened = false;
				OnDeviceStateImpl(PORT_CLOSED);
			}
		}
		m_bPortOpened = false;
		
		//OK, everything is torn down now, set up new port
		if (p != NULL)
		{
			m_spPremisePort = p;
			
			long bOpen=0;
			p->GetOpenStatus(&bOpen);
			if (bOpen)
			{
				OnDeviceStateImpl(PORT_OPEN_INUSE);
				SetPortInstance(NULL, false);
				//ATLTRACE2("   PORT_OPEN_INUSE\n");
				return S_FALSE;
			}
			if (FAILED(OnDeviceStateImpl(PORT_OPENING)))
			{
				OnDeviceStateImpl(PORT_OPEN_FAILED);
				SetPortInstance(NULL, false);
				//ATLTRACE2("   PORT_OPEN_FAILED\n");
				return S_FALSE;
			}
			//reopen port
			hr = p->OpenPort(this);
#ifndef OPEN_SERIAL_ASYNC
			p->GetOpenStatus(&bOpen);
			if(!bOpen)
			{
				OnDeviceStateImpl(PORT_OPEN_FAILED);
				SetPortInstance(NULL, false);
				//ATLTRACE2("   PORT_OPEN_FAILED (OPEN_SERIAL_ASYNC)\n");
				return S_FALSE;
			}
			OnDeviceStateImpl(PORT_OPENED);
			m_bPortOpened = true;
			
			OnDeviceStateImpl(DEVICE_INIT);
#endif
		}
		//ATLTRACE2("   Returning 0x%p\n", hr);
		return hr;
	}
	void ClearBufferedCommands(bool bCopyCommands = false)
	{
		if (bCopyCommands)
		{
			for (int i=0;i<m_commands.GetSize();i++)
				m_commandsBackup.Add(m_commands[i]);
		}
		m_commands.RemoveAll();
		m_nLastCmdIDSent = 0;
		m_ackType = CACK_NONE;
		m_timerAck.StopTimer();
		m_dwLastCommandSentTick = 0;
		m_bCanSendCommand = true;
	}
	void ExitRecoveryMode()
	{
		RECOMODE reco = m_RecoveryMode;
		switch(reco)
		{
		case RECOMODE_NONE:
			break;
		case RECOMODE_INITPENDING:
		case RECOMODE_INIT:
			if (m_commands.GetSize() == 0) //through with init
			{
				m_RecoveryMode = RECOMODE_NONE;
				//put all the original commands back on the queue
				for (int i=0;i<m_commandsBackup.GetSize();i++)
					m_commands.Add(m_commandsBackup[i]);
				m_commandsBackup.RemoveAll();
			}
			else
				m_RecoveryMode = RECOMODE_INITPENDING;
			break;
		}
	}
	bool CanSend()
	{
		return m_bCanSendCommand;
	}
	void SetAckReceived(bool bTrySend = true)
	{
		CSLock lock(&m_csBPD);
		if (m_ackType != CACK_TIMER)
		{
			WriteDataToLog((BYTE*)"SetAckReceived", -1, LoggerInfo, false);
			m_ackType = CACK_NONE;
			m_timerAck.StopTimer();
			m_dwLastCommandSentTick = 0;
			if (m_commands.GetSize() != 0)
			{
				_command& cmd = m_commands[0];
				if (cmd.bSent)
					m_commands.RemoveAt(0);
			}
			m_bCanSendCommand = true;
			ExitRecoveryMode();
			if (bTrySend)
				TrySend();
		}
	}
	void CancelAck()
	{
		CSLock lock(&m_csBPD);
		WriteDataToLog((BYTE*)"CancelAck", -1, LoggerInfo, false);
		m_ackType = CACK_NONE;
		m_timerAck.StopTimer();
		m_dwLastCommandSentTick = 0;
	}
	void SetAckStatus(bool b)
	{
		CSLock lock(&m_csBPD);
		m_bCanSendCommand = b;
	}
	UINT_PTR GetLastCmdSent()
	{
		CSLock lock(m_csBPD);
		return m_nLastCmdIDSent;
	}
	void SetTextModeTerminators(BYTE* pb, int nCount)
	{
		_ASSERTE(nCount != 0);
		CSLock lock(m_csBPD);
		m_arrTerms.RemoveAll();
		for (int i=0;i<nCount;i++)
			m_arrTerms.Add(pb[i]);
	}
	bool IsTerminator(char ch)
	{
		for (int i=0;i<m_arrTerms.GetSize();i++)
		{
			if (m_arrTerms[i] == (BYTE)ch)
				return true;
		}
		return false;
	}

	virtual unsigned long ProcessReadBuffer(BYTE* pBuf, DWORD dw, IPremisePort* pPort)
	{
		//						state	cr/lf	other character
		//					-----------------------------------
		// beginning of line	  0		 0		1/b	
		// saw a noti, wait4 \n	  1		 0/c	1
		//
		// Entries in table are newstate/action pairs
		// These map to simple integer entries below
		
		//0			go to state 0, update processed
		//1
		//2 - 0/c - goto state 0, send command
		//3 - 1/b - goto state 1, record current beg of line
		
		const int machine[2][2] = {{0,3},{2,1}};
		
		BYTE chTemp;
		BYTE* p = pBuf;
		BYTE* pszProcessed = pBuf;
		BYTE* pszLine;
		int nState=0;
		int nType = 0;
		while (p != (pBuf+dw))
		{
			if (IsTerminator(*p))
				nType = 0;
			else
				nType = 1;
			switch(machine[nState][nType])
			{
			case 0:
				nState = 0;
				pszProcessed = (p+1);
				break;
			case 1:
				nState = 1;
				break;
			case 2: //send command, go to state 0
				chTemp = *p;
				*p = NULL;
				ProcessLine((LPCSTR)pszLine);
				// try to write in case the process line
				// set the ack state
				TrySend();
				*p = chTemp;
				nState = 0;
				pszProcessed = (p+1);
				break;
			case 3:	//beginning of response line
				nState = 1;
				pszLine = p; //record beginning of line
				break;
			}
			p++;
		}
		//return how much was processed
		return pszProcessed - pBuf;
	}
	
	//Scenario -- if someone adds a serial device but doesn't bind to a port, 
	//we don't want to crash and we don't want to add a bunch of buffered commands
	bool CheckPort()
	{
		if (m_spPremisePort == NULL)
			return false;
		if (!m_bPortOpened)
			return false;
		return true;
	}
	
	void TrySend()
	{
		CSLock lock(&m_csBPD);
		if (m_bCanSendCommand)
			SendNextCommandImmediate();
	}
	
	void ResendLastCmdImmediate()
	{
		CSLock lock(&m_csBPD);
		SendNextCommandImmediate();
	}

	bool CompareLastCmd(const BYTE* pb, ULONG cb)
	{
		bool b = false;
		CSLock lock(&m_csBPD);
		if (m_commands.GetSize() != 0)
		{
			if (m_commands[0].nLen == cb)
				b = memcmp(pb, m_commands[0].pstr, cb) == 0;
		}
		return b;
	}
	
	// Instead of just using the critical section and clearing it, we
	// mark it to be cleared and then clear it the next time we get data
	// This is because we can deadlock in some cases if we try to get the 
	// critical section.
	void ClearInputBuffer()
	{
		m_bClearBuffer = true;
	}
	
protected:
	void SendNextCommandImmediate()
	{
		if (!CheckPort())
			return;
		if (m_commands.GetSize() != 0)
		{
			m_bCanSendCommand = false;
			//send it
			_command& cmd = m_commands[0];
			OnBufferedWrite(cmd);
			m_ackType = cmd.ackType;
			m_dwLastCommandSentTick = GetTickCountNonZero();//zero is special value
			_ASSERTE(cmd.dwAutoAck != 0);
			m_timerAck.StartSingleShotTimer(cmd.dwAutoAck);
			cmd.bSent = true;
			SendImmediateData(cmd.pstr, cmd.nLen, cmd.nCmdID);
		}
	}

	HRESULT SendData(BYTE* p, int nLen, COMMAND_ACK ack, DWORD dwWait, UINT_PTR nCommandID)
	{
		//we check here so we don't block if we are in the middle of opening a port.
		//This could take a while if the network port doesn't actually exist.
		if (!CheckPort())
			return S_FALSE;
		CSLock lock(&m_csBPD);
		if (!CheckPort())
			return S_FALSE;
		RECOMODE reco = m_RecoveryMode;
		switch(reco)
		{
		//add stuff to main command queue normally and when reiniting
		case RECOMODE_NONE:
		case RECOMODE_INIT:
			m_commands.Add(_command(p, nLen, nCommandID, ack, dwWait));
			break;
		//add stuff to backup queue when in the middle of waiting for init to finish
		case RECOMODE_INITPENDING:
			m_commandsBackup.Add(_command(p, nLen, nCommandID, ack, dwWait));
			break;
		}
		TrySend();
		return S_OK;
	}

	void SetFailure()
	{
		if (m_spSite != NULL)
		{
			m_spSite->SetValue(L"CommunicationFailure", &CComVariant(true));
			SYSTEMTIME st;
			ZeroMemory(&st, sizeof(SYSTEMTIME));
			GetLocalTime(&st);
			CComVariant varTime;
			varTime.vt = VT_DATE;
			varTime.date = 0;
			SystemTimeToVariantTime(&st, &varTime.date);
			m_spSite->SetValue(L"LastFailureTime", &varTime);
		}
	}

	void DoRecovery()
	{
		OnRecovery();
		CSLock lock(&m_csBPD);
		if (GetTimeElapsed(m_dwLastRecovery) > s_dwRecoveryCycle) // more than 5 minute since last recovery
		{
			m_nRecovery = 0;
		}
		else if (m_nRecovery == 1)
		{
			//log error
			SetFailure();
			LogPortEvent(L"Driver failed to get response from device", 
				_T("%s on %s port is being reset because of no response."));
			WriteDataToLog((BYTE*)"Driver failed to get response from device", -1, LoggerError, false);
		}
		m_dwLastRecovery = GetTickCountNonZero();
		m_nRecovery = m_nRecovery+1; //++ causes compiler error when protect is on

		//ok, remember the port, close the port, copy the data, open the port
		//and put the data back
		ClearBufferedCommands(m_RecoveryMode == RECOMODE_NONE); //back up commands in buffer if not in recovery mode already
		_ASSERTE(m_commands.GetSize() == 0);
		//every ten consecutive recoveries clear out any backup
		//otherwise we will gradually fill up all available memory
		if (m_nRecovery%10 == 0)
			m_commandsBackup.RemoveAll();

		m_RecoveryMode = RECOMODE_NONE; //closing a port will exit the recovery mode
		CComPtr<IPremisePort> spPort = m_spPremisePort;
		_ASSERTE(spPort != NULL);
		SetPortInstance(NULL);
		//no matter what, our recovery mode is now this
		m_RecoveryMode = RECOMODE_INIT;
		Sleep(100); //async ports can take a bit to close...

		SetPortInstance(spPort);

		ExitRecoveryMode();
		//otherwise wait for reset stuff to go through before exiting
		//if we don't do this we'll end up copying all of the reset commands
		//every time we come in here.
	}
	void OnHeartbeatTimer(UINT_PTR dw)
	{
		m_timerHeartbeat.StopTimer();
		OnHeartbeat();
		CSLock cslock(&m_csBPD);

		//if this is first time into heartbeat, don't want to do one right off the bat
		if (m_dwLastPing == 0)
			m_dwLastPing = GetTickCountNonZero();

		if (m_ackType == CACK_NONE) //not waiting on anything, only send heartbeat at this time
		{
			_ASSERTE(m_dwLastCommandSentTick == 0);
			if (GetTimeElapsed(m_dwLastPing) > s_dwPingFreq)
			{
				if (!m_bPortOpened) //if port isn't open, try to reopen it from the the network object
					OpenPort();
				else
				{
					WriteDataToLog((BYTE*)"WatchDog", -1, LoggerInfo, false);
					if (!OnPing()) //let driver do something if it wants
						DoRecovery();
				}
				m_dwLastPing = GetTickCountNonZero();
			}
		}
		m_timerHeartbeat.StartTimer(s_dwHeartbeat);
	}
	void OnAckTimer(UINT_PTR dw)
	{
		m_timerAck.StopTimer();
		CSLock cslock(&m_csBPD);
		COMMAND_ACK ackType = m_ackType;
		switch (ackType)
		{
		case CACK_TIMER: //waiting for timer to expire for autoack
			m_ackType = CACK_NONE; //SetAckReceived returns immed if this is CACK_TIMER
			SetAckReceived();
			break;
		case CACK_TIMEOUT: //waiting for explicit ack, or for timer to expire for autoack
			if (OnAckTimeout())
				SetAckReceived();
			break;
		case CACK_WRITE: //waiting for write to complete, failed to happen in allowed time
		case CACK_EXPLICIT: //waiting for explicit ack, failed to happen in allowed time
			DoRecovery();
			break;
		};
	}
	STDMETHOD(SetSite)(IUnknown *pSite)
	{
		//ATLTRACE2("CPremiseBufferedPortDeviceBase::SetSite(0x%p)\n", pSite);
		CSLock cslock(&m_csBPD);		

		if (pSite == NULL)
		{
			m_timerHeartbeat.StopTimer();
			SetPortInstance(NULL);
		}
		HRESULT hr = CPremiseSubscriber::SetSite(pSite);
		if (FAILED(hr))
			return hr;
		if (m_spSite == NULL)
			return S_OK;

		OpenPort();
		m_timerHeartbeat.StartTimer(s_dwHeartbeat);
		return S_OK;
	}
	virtual HRESULT OpenPort() = 0;
	void LogPortEvent(LPOLESTR pszEventName, LPCTSTR pszFmt)
	{
		if (m_spSite == NULL)
			return;

		USES_CONVERSION;
		//first off, log the error
		CComVariant varName;
		m_spSite->GetValue(L"Name", &varName);
		TCHAR buf[256];
		wsprintf(buf, pszFmt, OLE2T(varName.bstrVal), OLE2T(m_bstrPortName));
		m_spSite->RaiseException(NULL /*Exception class*/, pszEventName, T2OLE(buf), 0,E_FAIL, NULL);
		ATLTRACE("%s -- %s\n", OLE2T(pszEventName), buf);
	}
	//override so that we can log data if needed
	STDMETHOD(OnValueChanged)(long subscriptionID, long transactionID, long propagationID, long 
		controlCode, IPremiseObject *pObject, BSTR bstrPropertyName, VARIANT newValue, VARIANT oldValue)
	{
		WritePropertyChangedToLog(pObject, bstrPropertyName, newValue);
		return IPremiseNotifyImpl::OnValueChanged(subscriptionID, transactionID, propagationID, 
			controlCode, pObject, bstrPropertyName, newValue, oldValue);
	}
};

class __declspec(novtable) CPremiseBufferedPortDevice : 
	public CPremiseBufferedPortDeviceBase
{
public: //required overrides
	virtual HRESULT OnConfigurePort(IPremiseObject* pPort) = 0;
public:
	virtual HRESULT OpenPort()
	{
		//ATLTRACE2("CPremiseBufferedPortDevice::OpenPort\n");
		if (m_spSite == NULL)
		{
			//ATLTRACE2("   m_spSite == NULL\n");
			return E_FAIL;
		}

		CComVariant var;
		HRESULT hr = m_spSite->GetValue(L"Network", &var);
		if (FAILED(hr))
		{
			//ATLTRACE2("   Couldn't get Network\n");
			return hr;
		}
		CComBSTR bstr;
		m_spSite->get_Name(&bstr);
		//ATLTRACE2("   name = %S\n", bstr);
		if ((var.vt == VT_UNKNOWN) || (var.vt == VT_DISPATCH))
		{
			CComQIPtr<IPremiseObject> spPort = var.punkVal;
			if (spPort != NULL)
				return OpenPremisePort(spPort);
			else
			{
				//ATLTRACE2("   spPort == NULL\n");
			}
		}
		//ATLTRACE2("   Return E_FAIL\n");
		return E_FAIL;
	}
	HRESULT STDMETHODCALLTYPE OnNetworkChanged(IPremiseObject *pObject, VARIANT newValue)
	{
		//ATLTRACE2("CPremiseBufferedPortDevice::OnNetworkChanged(0x%p)\n", newValue.pdispVal);
		CComPtr<IPremiseObject> spPort;
		if(newValue.pdispVal != NULL)
			newValue.pdispVal->QueryInterface(&spPort);
		CSLock cslock(&m_csBPD);		
		ClearBufferedCommands(); //reset everything
		m_spSite->SetValue(L"CommunicationFailure", &CComVariant(false));
		CComVariant varTime;
		varTime.vt = VT_DATE;
		varTime.date = 0;
		m_spSite->SetValue(L"LastFailureTime", &varTime);

		HRESULT hr = OpenPremisePort(spPort);
		return S_OK;
	}
	HRESULT OpenPremisePort(IPremiseObject* pPort, bool bCloseOldPort = true)
	{
		//ATLTRACE2("CPremiseBufferedPortDevice::OpenPremisePort(0x%p, %d)\n", pPort, bCloseOldPort);
		CSLock cslock(&m_csBPD);		
		CComQIPtr<IPremisePort> spPremisePort;

		_ASSERTE(m_pPIS != NULL);
		//ATLTRACE2("   this   = 0x%p\n", this);
		//ATLTRACE2("   m_pPIS = 0x%p\n", m_pPIS);
		m_pPIS->SetCallback(this);
		m_pPIS->SetSite(pPort);

		//ATLTRACE2("   Calling SetPortInstance(NULL)\n");
		SetPortInstance(NULL);
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
				//ATLTRACE2("   Already open\n");
				OnDeviceStateImpl(PORT_OPEN_INUSE);
				SetPortInstance(NULL, false);
				return S_FALSE;
			}
			//ATLTRACE2("   Calling OnConfigurePort\n");
			OnConfigurePort(pPort);
			_ASSERTE(spPremisePort);
			//ATLTRACE2("   Calling SetPortInstance\n");
			hr = SetPortInstance(spPremisePort, bCloseOldPort);
			if (hr == S_OK)
			{
				CComVariant var;
				HRESULT hr = pPort->GetValue(L"Name", &var);
				if (FAILED(hr) || (var.vt != VT_BSTR))
					m_bstrPortName = L"unknown";
				else
					m_bstrPortName = var.bstrVal;

				//ATLTRACE2("   m_bstrName = %S\n", m_bstrPortName);
			}
		}
		return S_OK;
	}
};

class __declspec(novtable) CPremiseBufferedNetworkDevice : 
	public CPremiseBufferedPortDeviceBase
{
//assumes a device with IPAddress and IPPort properties
//e.g. derived from TCPTransport
public:
	virtual HRESULT OpenPort()
	{
		if (m_spSite == NULL)
			return E_FAIL;
		CComVariant varAddress;
		HRESULT hr = m_spSite->GetValue(L"IPAddress", &varAddress);
		if (FAILED(hr))
			return hr;
		CComVariant varPort;
		hr = m_spSite->GetValue(L"IPPort", &varPort);
		if (FAILED(hr))
			return hr;

		CComPtr<IPremiseNetworkPort> spPort;
		hr = spPort.CoCreateInstance(CLSID_PremiseIPPort);
		if (FAILED(hr))
			return hr;
		if ((varAddress.bstrVal == NULL) || (ocslen(varAddress.bstrVal)<7)) // 1.1.1.1
			return E_FAIL;

		spPort->SetPortSpy(m_spSite);

		hr = spPort->SetupPort(PREMISE_PROTOCOL_TCP, NULL, varAddress.bstrVal, varPort.lVal);
		if (FAILED(hr))
			return hr;

		hr = SetPortInstance(spPort);
		return hr;
	}
	HRESULT STDMETHODCALLTYPE OnIPPropChanged(IPremiseObject *pObject, VARIANT newValue)
	{
		SetPortInstance(NULL); //definitely close
		return OpenPort();
	}
};

class __declspec(novtable) CPremiseBufferedHandleDevice : 
	public CPremiseBufferedPortDeviceBase
{
//assumes a device with IPAddress and IPPort properties
//e.g. derived from TCPTransport
public:
	virtual HRESULT OpenPort()
	{
		if (m_spSite == NULL)
			return E_FAIL;
		CComVariant varName;
		HRESULT hr = m_spSite->GetValue(L"DeviceName", &varName);
		if (FAILED(hr))
			return hr;

		CComPtr<IPremiseHandlePort> spPort;
		hr = spPort.CoCreateInstance(CLSID_PremiseHandlePort);
		if (FAILED(hr))
			return hr;
		if ((varName.bstrVal == NULL) || (ocslen(varName.bstrVal)<1))
			return E_FAIL;

		spPort->SetPortSpy(m_spSite);

		hr = spPort->SetupPort(varName.bstrVal, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
			OPEN_EXISTING, FILE_FLAG_OVERLAPPED);
		if (FAILED(hr))
			return hr;

		hr = SetPortInstance(spPort);
		return hr;
	}
	HRESULT STDMETHODCALLTYPE OnDeviceNameChanged(IPremiseObject *pObject, VARIANT newValue)
	{
		SetPortInstance(NULL); //definitely close
		return OpenPort();
	}
};

class __declspec(novtable) CPremiseDriverImpl : public CPremiseSubscriber
{
public:
	
	//have to play this trick with CAdapt because CSimpleArray takes the 
	//address of its items.  CComPtr overloads the & operator and ASSERTS that the
	//value is NULL.  This causes various ASSERTS in normal situations
	typedef CAdapt<CComPtr<IObjectWithSite> > SPIOWS;
	CSimpleArray<SPIOWS> m_vecControllers;
	
public: //Overrides
	// CreateController must be overriden in derived class
	virtual HRESULT CreateControllerForSite(IPremiseObject* pObject, IObjectWithSite** ppSite, bool bFirstTime) = 0;
	
public:
	// Maintain list of controllers associated with this driver
	HRESULT STDMETHODCALLTYPE OnObjectCreated(IPremiseObject *pContainer, IPremiseObject *pCreatedObject)
	{
		//ATLTRACE2("CPremiseDriverImpl::OnObjectCreated(0x%p, 0x%p)\n", pContainer, pCreatedObject);
		if (pContainer == m_spSite)
		{
			//ATLTRACE2("   Calling AddController\n");
			return AddController(pCreatedObject, true);
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnObjectDeleted(IPremiseObject *pContainer, IPremiseObject *pDeletedObject)
	{
		if (pContainer == m_spSite)
			return RemoveController(pDeletedObject);
		return S_OK;
	}
	
	HRESULT AddController(IPremiseObject* pSite, bool bFirstTime)
	{
		CComPtr<IObjectWithSite> spOWS;
		HRESULT hr = CreateControllerForSite(pSite, &spOWS, bFirstTime);
		if (FAILED(hr) || spOWS == NULL)
			return hr;
		
		hr = spOWS->SetSite(pSite);
		if (FAILED(hr))
			return hr;
		
		return m_vecControllers.Add(SPIOWS(spOWS)) ? S_OK : E_FAIL;
	}
	HRESULT RemoveController(IPremiseObject* pSite)
	{
		int i;
		for (i=0;i<m_vecControllers.GetSize();i++)
		{
			CComPtr<IPremiseObject> spSite;
			IObjectWithSite* pOWS = m_vecControllers[i].m_T;
			pOWS->GetSite(IID_IPremiseObject, (void**)&spSite);
			if (pSite == spSite)
			{
				pOWS->SetSite(NULL);
				m_vecControllers.RemoveAt(i);
				return S_OK;
			}
		}
		return E_FAIL;
	}

	virtual HRESULT OnBrokerAttach()
	{
		//ATLTRACE2("CPremiseDriverImpl::OnBrokerAttach\n");
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
			//ATLTRACE2("   Calling AddController\n");
			CComQIPtr<IPremiseObject> spElem(varElem.pdispVal);
			hr = AddController(spElem, false);
		}

		return S_OK;
	}
	virtual HRESULT OnBrokerDetach()
	{
		// clean up all old objects
		int i;
		for (i=0;i<m_vecControllers.GetSize();i++)
			m_vecControllers[i].m_T->SetSite(NULL);
		m_vecControllers.RemoveAll();

		return S_OK;
	}
};

#endif
