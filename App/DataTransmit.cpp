/*
 * DataTransmit.cpp
 *
 *  Created on: 21. 4. 2026
 *      Author: radek
 */

#include  "DataTransmit.hpp"
#include  "RadioParams.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include "Common.hpp"


uint16_t DataTransmit::timeout = 0;
bool DataTransmit::MasterMode = false;
bool DataTransmit::RequestSent = false;
bool DataTransmit::DataAvailable = false;
bool DataTransmit::DataOverload = false;
const struct Radio_s* DataTransmit::RadioDriver = nullptr;
RadioEvents_t DataTransmit::RadioEvents = {};
TimerHandle_t DataTransmit::CadTimer= nullptr;
//TimerHandle_t DataTransmit::RxTimeoutTimer = nullptr;
Packet DataTransmit::packet = {};
bool DataTransmit::SlaveNotResponding = false;
Packet::PacketType DataTransmit::DataType = Packet::Type_undefined;


[[maybe_unused]] 
void CadTimerCallback(TimerHandle_t xTimer){   
   DataTransmit::RadioDriver->Standby( );
   DataTransmit::RadioDriver->SetChannel(CHANNEL);
   DataTransmit::RadioDriver->StartCad( );
}

extern "C"  [[maybe_unused]] 
void RadioCadTimeoutIrq( void *context ){
	if( DataTransmit::MasterMode == true){
		return; // pokud jsme v master modu, neprovádíme CAD
	}
	DataTransmit::RadioDriver->Standby( );
	DataTransmit::RadioDriver->StartCad( );
	#if RADIO_DEBUG_PRINT 
	printf("Start CAD\n");
	#endif
}


extern "C" void OnCadDone( bool channelActivityDetected ){
	if(channelActivityDetected){
		DataTransmit::RadioDriver->Rx(DataTransmit::RxReceiverMaxRunTime);
		BaseType_t hpw = pdFALSE;
		auto Ok = xTimerStopFromISR(DataTransmit::CadTimer,&hpw);
		configASSERT(Ok == pdPASS);
		#if RADIO_DEBUG_PRINT 
		printf(">>>>Start RX\n");
		#endif
	}else{
		// Channel is clear, proceed with transmission
		BaseType_t hpw = pdFALSE;
		auto Ok = xTimerStartFromISR(DataTransmit::CadTimer,&hpw);
		configASSERT(Ok == pdPASS);
	}
}

extern "C" void OnTxDone(void){
	if( DataTransmit::MasterMode == true){
		DataTransmit::RadioDriver->Rx(DataTransmit::ResponseTimeout); // Po odeslání požadavku přepneme rádio do přijímacího režimu
		#if RADIO_DEBUG_PRINT 
		printf("Transmission done, starting RX\n");
		#endif
		return; 
	}
	DataTransmit::RadioDriver->Standby( );
	DataTransmit::RadioDriver->StartCad( );
	#if RADIO_DEBUG_PRINT 
	printf("Transmission done, restarting CAD\n");
	#endif
};

/* Callback functions - Rx complete */
extern "C" void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo){
	DataTransmit::RequestSent = false;
	
	#if RADIO_DEBUG_PRINT 
	printf("Received packet: size=%u, rssi=%d, snr=%d time-out=%d\n", size, rssi, LoraSnr_FskCfo,DataTransmit::timeout);
	#endif
	DataTransmit::timeout = 0;
	
	// kopírujeme payload do packetu pro další zpracování
	std::array<uint8_t, Packet::max_packet_size> buffer{};
	std::memcpy(buffer.data(), payload, size);
  
	if(DataTransmit::packet.ParsePacket(buffer)==true){
		if(DataTransmit::DataAvailable){
			DataTransmit::DataOverload = true;
		}
		else DataTransmit::DataOverload = false;
		DataTransmit::DataAvailable = true;
		DataTransmit::DataType = DataTransmit::packet.Type_out; 
	}
}

extern "C" void OnTxTimeout(void){
	if( DataTransmit::MasterMode == true){
		return; // pokud jsme v master modu, neprovádíme CAD
	}
	DataTransmit::RadioDriver->Standby( );
	DataTransmit::RadioDriver->StartCad( );
	#if RADIO_DEBUG_PRINT 
	printf("TX Timeout, restarting CAD\n");
	#endif
}

extern "C" void OnRxTimeout(void){
	if( DataTransmit::MasterMode == true){
		if(++DataTransmit::timeout > DataTransmit::MAX_TX_ATTEMPTS){ // Po xx neúspěšných pokusech o příjem dat považujeme slave za nereagujícího
			DataTransmit::RadioDriver->Standby( );
			DataTransmit::RadioDriver->Rx(DataTransmit::ResponseTimeout);
			DataTransmit::timeout = 0;
			if(DataTransmit::RequestSent){
				DataTransmit::RequestSent = false;
				DataTransmit::SlaveNotResponding = true;
			}			
		}
		#if RADIO_DEBUG_PRINT 
		printf("Rx timeout,restarting RX, time-out= %d\n",DataTransmit::timeout);
		#endif		
		return; // pokud jsme v master modu, neprovádíme CAD
	}
	DataTransmit::RadioDriver->Standby( );
	DataTransmit::RadioDriver->StartCad( );
	#if RADIO_DEBUG_PRINT 
	printf("RX Timeout, restarting CAD\n");
	#endif
}

extern "C" void OnRxError(void){
	if( DataTransmit::MasterMode == true){
		if(DataTransmit::RequestSent){
			DataTransmit::RequestSent = false;
			DataTransmit::SlaveNotResponding = true;
		}
		return; // pokud jsme v master modu, neprovádíme CAD
	}
	DataTransmit::RadioDriver->Standby( );
	DataTransmit::RadioDriver->StartCad( );
	#if RADIO_DEBUG_PRINT 
	printf("RX Error, restarting CAD\n");
	#endif
}

/* init  radio */
void DataTransmit::Init(const struct Radio_s *Radio_,bool MasterMode_){
	assert(Radio_ != nullptr); 
	RadioDriver = Radio_;
	MasterMode = MasterMode_;
	
	/* events  setup */
	RadioEvents.TxDone = OnTxDone;
	RadioEvents.RxDone = OnRxDone;
	RadioEvents.TxTimeout = OnTxTimeout;
	RadioEvents.RxTimeout = OnRxTimeout;
	RadioEvents.RxError = OnRxError;
	RadioEvents.CadDone = OnCadDone;
	RadioDriver->Init(&RadioEvents);

	RadioDriver->SetModem(MODEM_LORA);
	RadioDriver->SetPublicNetwork(true);
	//RadioDriver->RxBoosted(false);	
	/* radio setup */
	RadioDriver->SetTxConfig(MODEM_LORA,TX_POWER,0,BANDWIDTH,SPREED_FACTOR,CODE_RATE,PREAMBLE_LEN,FIX_LEN,CRC_ON,
	  									FREQ_HOP_ON,HOP_PERIODE,SYMBOL_INVERTED,10000);
	RadioDriver->SetChannel(CHANNEL);
	RadioDriver->SetRxConfig(MODEM_LORA, BANDWIDTH,SPREED_FACTOR, CODE_RATE,0,PREAMBLE_LEN,SYMB_TIMEOUT,FIX_LEN,PAYLOAD_LEN,
	  										CRC_ON, FREQ_HOP_ON, HOP_PERIODE,SYMBOL_INVERTED,true);
	RadioDriver->Standby( );
 	
	/* CAD sampler timer setup */
	if(MasterMode == false){
		CadTimer = xTimerCreate(
        "Cad sampler",
        pdMS_TO_TICKS(CAD_sample ),
        pdTRUE,
        nullptr,
        CadTimerCallback);
    	configASSERT(CadTimer != nullptr);
	}
	else{
		/*
		// one shot timer - response overtime 
		RxTimeoutTimer = xTimerCreate(
        "Rx timeout",
        pdMS_TO_TICKS(ResponseTimeout),
        pdFALSE,
        nullptr,
        RxTimeoutTimerCallback);
    	configASSERT(RxTimeoutTimer != nullptr);
		*/
	}
		
	DataAvailable = false;		
	DataOverload = false;		
	#if RADIO_DEBUG_PRINT 
	printf("DataTransmit initialized\n");
	#endif
}

/* send data  request  */
bool DataTransmit::SendRquest(Packet::PacketType Type){
	
	/* create packet */
	if(!packet.CreatePacket(Type)){
		#if RADIO_DEBUG_PRINT 
		printf("Packet creation failed!\n");
		#endif
		return false; // Packet creation failed
	}
	/* send packet */
	#if RADIO_DEBUG_PRINT 
	printf("Raw data sent: ");
	for(size_t i = 0; i < packet.Packet_output.size(); ++i){
		printf("%02X ", packet.Packet_output[i]);
	}
	printf("\n");
	#endif
	
	RequestSent = true;
	SlaveNotResponding = false;
	RadioDriver->Standby( );
	RadioDriver->Send(packet.Packet_output.data(), packet.Packet_output.size());
	return true;
}

