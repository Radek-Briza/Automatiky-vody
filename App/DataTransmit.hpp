/*
 * DataTransmit.hpp
 *
 *  Created on: 21. 4. 2026
 *      Author: radek
 */

#pragma once 

#include "radio.h"
#include "timer.h"
#include "Packet.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"


#ifdef __cplusplus
extern "C" {
#endif

void RadioCadTimeoutIrq(void *context);
void OnCadDone(bool channelActivityDetected);
void OnTxDone(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo);
void OnRxTimeout(void);
void OnRxError(void);
void OnTxTimeout(void);
void RadioCadTimeoutIrq( void *context );
void ReceiveTimeout(void *context);
#ifdef __cplusplus
}
#endif

class DataTransmit {	
public:
	static DataTransmit& GetInstance() {
		static DataTransmit instance;
		return instance;
	}
	void Init(const struct Radio_s *Radio_, bool MasterMode_ = false);
	bool SendRquest(Packet::PacketType Type);
	Packet::PacketType GetReceivedDataType() const { return DataType; }
	const std::vector<uint8_t>& GetReceivedPayload() const { return packet.Payload_output; }	
	static bool DataAvailable;
	static bool DataOverload; 
	static bool SlaveNotResponding;
	static Packet::PacketType ReceivedDataType;
	
private:
	DataTransmit() = default;
	~DataTransmit() = default;
	DataTransmit(const DataTransmit&) = delete;
	DataTransmit& operator=(const DataTransmit&) = delete;
	DataTransmit(DataTransmit&&) = delete;
	DataTransmit& operator=(DataTransmit&&) = delete;

	const static uint32_t MAX_TX_ATTEMPTS = 10; // Maximální počet pokusů o příjem dat, po kterém považujeme slave za nereagujícího
	const uint32_t MaxPayloadSize = Packet::max_packet_size;
	static const struct Radio_s *RadioDriver;
	static const uint32_t CAD_sample = 200U; /* ms*/
	static const uint32_t RxReceiverMaxRunTime =  2000;	
	static  const uint32_t ResponseTimeout = 1800; /* ms*/
	static RadioEvents_t RadioEvents;
	static TimerHandle_t CadTimer;
	static TimerHandle_t RxTimeoutTimer;
	static Packet packet;
    static Packet::PacketType DataType; // Proměnná pro uložení typu dat z příchozího packetu
	static bool MasterMode;
	static bool RequestSent;
	static uint16_t timeout;
	
	friend void RadioCadTimeoutIrq(void *context);
	friend void OnCadDone(bool channelActivityDetected);
	friend void OnTxTimeout(void);
	friend void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo);
	friend void OnRxTimeout(void);
	friend void OnRxError(void);
	friend void OnTxDone(void);
	friend void RxTimeoutTimerCallback(TimerHandle_t xTimer);
	friend void CadTimerCallback(TimerHandle_t xTimer);
};


