
#include "PumpControl.hpp"
#include "Main.h" // IWYU pragma: keep.
#include <cstdio>
#include "Common.hpp"


//std::function<void(bool)> PumpControler::ErrorLedControl=nullptr;
//std::function<void(bool)> PumpControler::RunLedControl=nullptr;
std::function<void(bool)> PumpControler::PumpControlPin=nullptr;
bool PumpControler::PumpRun=false ;
bool PumpControler::ErrorCondition=false ;
TimerHandle_t PumpControler::PumpRunTimer=nullptr; 
QueueHandle_t PumpControler::QueuePumpControl=nullptr;
Message PumpControler::msgDisplay={};
Message PumpControler::msgPumpControl ={};
bool PumpControler::AutomaticModeOn = false;
uint16_t  PumpControler::LevelActual = 0;
uint16_t  PumpControler::LevelOld = 0;
bool PumpControler::StoreNewLevel = false;


/* pump overtime  handler */
[[maybe_unused]] 
void PumpOvertimerCallback(TimerHandle_t xTimer){
    if(PumpControler::PumpRun){
       
        /* test level increase */
        printf("Pump overtime - level actual: %u, level old: %u\r\n", static_cast<unsigned int>(PumpControler::LevelActual), static_cast<unsigned int>(PumpControler::LevelOld));
        if(PumpControler::LevelActual > PumpControler::LevelOld+1){
            PumpControler::LevelOld = PumpControler::LevelActual;
            auto Ok = xTimerStart(PumpControler::PumpRunTimer,0U);
            configASSERT(Ok == pdPASS);
            return;
        }

        #if APP_DEBUG_PRINT
        printf("Pump overtime - OFF\r\n");
        #endif
       
        /* error status for pump control task */
        PumpControler::msgPumpControl.MsgType = MsgDataType::PumpError;
        PumpControler::msgPumpControl.Data = 1;  // persist 
        auto ok = xQueueSend(
        PumpControler::QueuePumpControl,
        & PumpControler::msgPumpControl ,
        pdMS_TO_TICKS(10)
        );
        configASSERT(ok  == pdPASS) ;

        /* error status for  display */
        PumpControler::msgDisplay.MsgType = MsgDataType::PumpError;
        PumpControler::msgDisplay.Data = 1;  // persist 
        ok = xQueueSend(
        QueueDisplay,
        & PumpControler::msgDisplay ,
        pdMS_TO_TICKS(10)
        );
        configASSERT(ok  == pdPASS) ;
    }
}

/* init  class */
void PumpControler::Init(QueueHandle_t &QueuePumpControl_,
                          std::function<void(bool)> PumpControlPin_ ){
 
        PumpControlPin = std::move(PumpControlPin_);
        QueuePumpControl = QueuePumpControl_;

        /* timer init */
        PumpRunTimer = xTimerCreate(
        "Pump timer",
        pdMS_TO_TICKS(PUMP_MAX_RUN_TIME ),
        pdFALSE,
        nullptr,
        PumpOvertimerCallback);
    	configASSERT(PumpRunTimer != nullptr);


}

void PumpControler::ControlPump(){
    configASSERT(PumpControler::PumpControlPin  != nullptr) ;
    configASSERT(gButtonQueue != nullptr);
    DisplayMessageType NewMessage = DisplayMessageType::NoMessage;


    /* automat LED on/off*/
        if(PumpControler::AutomaticModeOn){
            LedController::SetMode(
                LedController::Leds::AutomatOnLed,
                LedController::LedMode::On);
            }
            else{
                LedController::SetMode(
                LedController::Leds::AutomatOnLed,
                LedController::LedMode::Off);
            }

    /* get message  from radio module and timer  */
    Message StatusMsg;
    auto ok= xQueueReceive(
                QueuePumpControl,
                &StatusMsg,
                0
            );

    /* new message - level status */
    if(ok == pdPASS){
       
        /* emergency off */
          if(StatusMsg.MsgType == MsgDataType::PumpError){
            if(ErrorCondition==false){
                ErrorCondition = true;
                PumpRun = false;
                PumpControlPin(false);
                LedController::SetMode(
                    LedController::Leds::ErrorLed,
                    LedController::LedMode::On);
                LedController::SetMode(
                    LedController::Leds::PumpOnLed,
                    LedController::LedMode::Off);
                LedController::SetMode(
					LedController::Leds::Buzzer,
					LedController::LedMode::Blink);
                NewMessage = DisplayMessageType::PumpStop;
                #if APP_DEBUG_PRINT
                printf("Pump OFF - error\r\n");
                #endif
            }
        }

        /* level value  */
         if(StatusMsg.MsgType == MsgDataType::LevelData ){
            LevelActual = StatusMsg.Data;
            if(StoreNewLevel == true) {
               StoreNewLevel = false ;
            LevelOld = LevelActual;
           }
         }

        /* level status */
        if(StatusMsg.MsgType == MsgDataType::LevelStatusData ){
            printf("<< LEVEL STATUS: %u >>\r\n", static_cast<unsigned int>(StatusMsg.Data));
            if(!ErrorCondition){
                switch (StatusMsg.Data & (LEVEL_L | LEVEL_UNDER_M | LEVEL_H)){
                    
                    /* under max and middle - hysteresis */
                    case 0:
                    break;

                    /* under max , turn ON pump*/
                    case LEVEL_L:
                    if(! PumpRun && AutomaticModeOn){
                        PumpRun = true;
                        auto Ok = xTimerStart(PumpRunTimer,0U);
                        configASSERT(Ok == pdPASS);
                        // gpio pump on 
                        PumpControlPin(true);
                        LedController::SetMode(LedController::Leds::PumpOnLed,LedController::LedMode::On);
                        NewMessage = DisplayMessageType::PumpRun;
                        LedController::SetMode(
					LedController::Leds::Buzzer,
					LedController::LedMode::OneShot);
                        StoreNewLevel = true;
                        #if APP_DEBUG_PRINT
                        printf("Pump ON(1)\r\n");
                        #endif
                    }
                    break;

                /* drop level  */
                case LEVEL_UNDER_M:
                if(!PumpRun && AutomaticModeOn){
                    PumpRun = true;
                    auto Ok = xTimerStart(PumpRunTimer,0U);
                    configASSERT(Ok == pdPASS);
                    LedController::SetMode(LedController::Leds::PumpOnLed,LedController::LedMode::On);
                    PumpControlPin(true);
                    StoreNewLevel = true;
                    NewMessage = DisplayMessageType::PumpRun;
                    LedController::SetMode(
					LedController::Leds::Buzzer,
					LedController::LedMode::OneShot);
                    #if APP_DEBUG_PRINT
                    printf("Pump ON(2)\r\n");
                    #endif
                    }           
                break;    
            
                /* max limit , turn off pump  */
                case LEVEL_H:
                 NewMessage = DisplayMessageType::MaxLevel;

                default:
                    if(PumpRun){
                        PumpRun = false;
                        auto Ok = xTimerStop(PumpRunTimer,0U);
                        configASSERT(Ok == pdPASS);
                        LedController::SetMode(LedController::Leds::PumpOnLed,LedController::LedMode::Off);
                        PumpControlPin(false);
                        LedController::SetMode(
				    LedController::Leds::Buzzer,
				    LedController::LedMode::OneShot);
                         NewMessage = DisplayMessageType::PumpStop;
                        #if APP_DEBUG_PRINT
                        printf("Pump OFF\r\n");
                        #endif
                    }          
                }
            }
        }
     }   
    
   

     /* message from button - possible unblock system   */
     MessageButton BtnMsg;
     ok= xQueueReceive(
                gButtonQueue,
                &BtnMsg,
                0
            );

    /* new message - button */
    if(ok == pdPASS){
        /* enable automatic mode - if  enable pin is active */
        if(BtnMsg.buttonId==2) {

        /* manual set pump on if long press */
         if(BtnMsg.event == ButtonEventType::LongPress ){
               LedController::SetMode(LedController::Leds::PumpOnLed,LedController::LedMode::On);
                    PumpControlPin(true);
                    PumpRun = true;
                    StoreNewLevel = true;
                    AutomaticModeOn = true;
                    NewMessage = DisplayMessageType::PumpRun;
                    LedController::SetMode(
					LedController::Leds::Buzzer,
					LedController::LedMode::OneShot);
                    /* send message to display if a some change */
                    auto ok = xQueueSend(
                    QueueDisplay,
                    & PumpControler::msgDisplay ,
                    pdMS_TO_TICKS(300)
                    );
                    configASSERT(ok  == pdPASS) ;
                    #if APP_DEBUG_PRINT
                    printf("Pump ON(3)\r\n");
                    #endif
         }

        /* enable  automatic mode */
           if(BtnMsg.event == ButtonEventType::Press && AutomaticModeOn == false){
                AutomaticModeOn = true;
              /*  LedController::SetMode(
                LedController::Leds::AutomatOnLed,
                LedController::LedMode::On); */
                BtnMsg.event = ButtonEventType::Release;
                printf("<<<< Automatika ON >>>>\r\n");
            }

            /* disable automatic mode */
            if(BtnMsg.event == ButtonEventType::Press && AutomaticModeOn == true){
                /* disable automatic mode */
                msgDisplay.MsgType = MsgDataType::AtomatikaOff;
                msgDisplay.Data = 0; // clear 
                auto ok = xQueueSend(
                QueueDisplay,
                &msgDisplay ,
                pdMS_TO_TICKS(300)
                );
                configASSERT(ok  == pdPASS) ;

                AutomaticModeOn = false;
                LedController::SetMode(
                LedController::Leds::AutomatOnLed,
                LedController::LedMode::Off);
                
                if(PumpRun  ){
                    PumpRun = false;
                    auto Ok = xTimerStop(PumpRunTimer,0U);
                    configASSERT(Ok == pdPASS);
                    LedController::SetMode(LedController::Leds::PumpOnLed,LedController::LedMode::Off);
                    PumpControlPin(false);
                }
                LedController::SetMode(
                LedController::Leds::PumpOnLed,
                LedController::LedMode::Off);
                #if APP_DEBUG_PRINT
                printf("<<<< Automatika OFF >>>>\r\n");
                #endif
            }
        }
    
        /* deblock */
        if(BtnMsg.buttonId==1 && BtnMsg.event == ButtonEventType::LongPress){
            if(PumpControler::ErrorCondition == true){
            PumpControler::ErrorCondition = false;
            LedController::SetMode(
                LedController::Leds::ErrorLed,
                LedController::LedMode::Off);
            LedController::SetMode(
					LedController::Leds::Buzzer,
					LedController::LedMode::Off);
            /* clear error status for  display */
            msgDisplay.MsgType = MsgDataType::PumpError;
            msgDisplay.Data = 0; // clear 
            auto ok = xQueueSend(
            QueueDisplay,
            &msgDisplay ,
            pdMS_TO_TICKS(300)
            );
            configASSERT(ok  == pdPASS) ;
            }
            #if APP_DEBUG_PRINT
            printf("<<<< Cepadlo odblokovano >>>>\r\n");
            #endif
        }

    }
    
     /* send message to display if a some change */
    if( NewMessage != DisplayMessageType::NoMessage){
        PumpControler::msgDisplay.MsgType = MsgDataType::PumpStatus;
        PumpControler::msgDisplay.Data = (NewMessage == DisplayMessageType::PumpRun) ? 1 : 0;
            auto ok = xQueueSend(
            QueueDisplay,
            & PumpControler::msgDisplay ,
            pdMS_TO_TICKS(300)
            );
            configASSERT(ok  == pdPASS) ;
    }
}
