#ifndef __BDPPACKET_H__
#define __BDPPACKET_H__

enum enumCommand {
	eSelectBonDriver = 0,
	eCreateBonDriver,
	eOpenTuner,
	eCloseTuner,
	eSetChannel1,
	eGetSignalLevel,
	eWaitTsStream,
	eGetReadyCount,
	eGetTsStream,
	ePurgeTsStream,
	eRelease,

	eGetTunerName,
	eIsTunerOpening,
	eEnumTuningSpace,
	eEnumChannelName,
	eSetChannel2,
	eGetCurSpace,
	eGetCurChannel,

	eGetTotalDeviceNum,
	eGetActiveDeviceNum,
	eSetLnbPower,

	eGetClientInfo,
};

enum enumOption {
	eNoOption       = 0x000,
	eDesireToUseB25 = 0x001,
};

#pragma pack(push, 1)

struct stPacketHead {
	BYTE m_bSync;
	BYTE m_bCommand;
	BYTE m_bReserved1;
	BYTE m_bReserved2;
	DWORD m_dwBodyLength;
};

struct stPacket {
	stPacketHead head;
	BYTE payload[1];
};

#pragma pack(pop)

#define SYNC_BYTE	0xff
class cPacketHolder {
#ifdef __BONDRIVERPROXY_H__
	friend class cProxyServer;
#elif defined(__BONDRIVER_PROXYEX_H__)
	friend class cProxyServerEx;
#else
	friend class cProxyClient;
#endif
	union {
		stPacket *m_pPacket;
		BYTE *m_pBuf;
	};
	size_t m_Size;
	BOOL m_bDelete;

	inline void init(size_t PayloadSize)
	{
		m_pBuf = new BYTE[sizeof(stPacketHead) + PayloadSize];
		m_bDelete = TRUE;
	}

public:
	cPacketHolder(size_t PayloadSize)
	{
		init(PayloadSize);
	}

	cPacketHolder(enumCommand eCmd, size_t PayloadSize)
	{
		init(PayloadSize);
		*(DWORD *)m_pBuf = 0;
		m_pPacket->head.m_bSync = SYNC_BYTE;
		SetCommand(eCmd);
		m_pPacket->head.m_dwBodyLength = ::htonl((DWORD)PayloadSize);
		m_Size = sizeof(stPacketHead) + PayloadSize;
	}

	cPacketHolder(enumCommand eCmd, size_t PayloadSize, enumOption bOptionFlag)
		: cPacketHolder(eCmd, PayloadSize)
	{
		SetOption(bOptionFlag);
	}

	~cPacketHolder()
	{
		if (m_bDelete)
			delete[] m_pBuf;
	}
	inline BOOL IsValid(){ return (m_pPacket->head.m_bSync == SYNC_BYTE); }
	inline BOOL IsTS(){ return (m_pPacket->head.m_bCommand == (BYTE)eGetTsStream); }
	inline enumCommand GetCommand(){ return (enumCommand)m_pPacket->head.m_bCommand; }
	inline void SetCommand(enumCommand eCmd){ m_pPacket->head.m_bCommand = (BYTE)eCmd; }
	inline void SetOption(enumOption eOpt){ m_pPacket->head.m_bReserved1 = (BYTE)eOpt; }
	inline DWORD GetBodyLength(){ return ::ntohl(m_pPacket->head.m_dwBodyLength); }
	inline void SetDeleteFlag(BOOL b){ m_bDelete = b; }
};

class cPacketFifo : protected std::queue<cPacketHolder *> {
	const size_t m_fifoSize;
	cCriticalSection m_Lock;
	cEvent m_Event;
	cEvent m_SpaceEvent;
	HANDLE m_hAbort;
	cPacketFifo &operator=(const cPacketFifo &);    // shut up C4512

public:
	cPacketFifo()
		: m_fifoSize(g_PacketFifoSize)
		, m_Event(TRUE, FALSE)
		, m_SpaceEvent(TRUE, TRUE)
		, m_hAbort(NULL)
	{}
	~cPacketFifo()
	{
		LOCK(m_Lock);
		while (!empty())
		{
			cPacketHolder *p = front();
			pop();
			delete p;
		}
	}

	void SetAbortEvent(HANDLE h)
	{
		m_hAbort = h;
	}

	void Push(cPacketHolder *p)
	{
		if ((m_fifoSize == 0) || !p->IsTS())
		{
			LOCK(m_Lock);
			push(p);
			m_Event.Set();
			if ((m_fifoSize != 0) && (size() >= m_fifoSize))
				m_SpaceEvent.Reset();
			return;
		}

		for (;;)
		{
			{
				LOCK(m_Lock);
				if (size() < m_fifoSize)
				{
					push(p);
					m_Event.Set();
					if (size() >= m_fifoSize)
						m_SpaceEvent.Reset();
					return;
				}
#if _DEBUG
				_RPT1(_CRT_WARN, "Packet Queue OVERFLOW : size[%d]\n", size());
#endif
			}

			HANDLE handles[2];
			DWORD waitCount = 0;
			if (m_hAbort != NULL)
				handles[waitCount++] = m_hAbort;
			handles[waitCount++] = (HANDLE)m_SpaceEvent;
			DWORD dw = ::WaitForMultipleObjects(waitCount, handles, FALSE, INFINITE);
			if (dw == WAIT_FAILED)
			{
				delete p;
				return;
			}
			if ((m_hAbort != NULL) && (dw == WAIT_OBJECT_0))
			{
				delete p;
				return;
			}
		}
	}

	void Pop(cPacketHolder **p)
	{
		LOCK(m_Lock);
		if (!empty())
		{
			*p = front();
			pop();
			if (empty())
				m_Event.Reset();
			if ((m_fifoSize != 0) && (size() < m_fifoSize))
				m_SpaceEvent.Set();
		}
		else
		{
			m_Event.Reset();
			if (m_fifoSize != 0)
				m_SpaceEvent.Set();
		}
	}

	HANDLE GetEventHandle()
	{
		return (HANDLE)m_Event;
	}

#if _DEBUG
	inline size_t Size()
	{
		return size();
	}
#endif
};

#endif	// __BDPPACKET_H__
