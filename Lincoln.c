//###########################################################################
//
// FILE:   Lincoln.c
//
// TITLE:  Lincoln Racing
//
//!  This project is for the control part of an autonomous driving vehicle.
//!
//
//###########################################################################

#include "include/main.h"

#pragma DATA_SECTION(ADC_Results,"Cla1DataRam1");
#pragma DATA_SECTION(Lincoln_Auto,"Cla1DataRam1");
#pragma DATA_SECTION(Lincoln_Auto_Handle,"Cla1DataRam1");

#pragma CODE_SECTION(adc_isr, "ramfuncs");
#pragma CODE_SECTION(ecan0_isr, "ramfuncs");
#pragma CODE_SECTION(ecap1_isr, "ramfuncs");
#pragma CODE_SECTION(ecap3_isr, "ramfuncs");

AUTO_Obj Lincoln_Auto;
AUTO_Handle Lincoln_Auto_Handle = &Lincoln_Auto;

Uint16 ADC_Results[16];
int count_init = 0;
struct ECAN_REGS ECanaShadow;
struct ECAN_REGS ECanaShadow_post;

Uint32 Message_RX_Count = 0;
Uint32 Message_TX_Count = 0;
Uint32 Message_RX_L = 0, Message_RX_H = 0;
Uint32 Message_TX_L = 0, Message_TX_H = 0;
Uint16 Message_RX_Index = 0;

int LED_Gimbal_Counter = 0;
int LED_Motor_Counter = 0;
int LED_CANBus_Counter = 0;

// system state machine
// 0: Initial
// 1: online
// 2: fault
int state_machine = 0; 


void main(void)
{
    // Initialize the DSP system
    Init_System();

    Init_Motor_Drvs();

    Init_Auto(Lincoln_Auto_Handle);

    // Forever loop
    for(;;){
    }
}


// ADC ISR 10 kHz. The main ISR for control loop
__interrupt void adc_isr(void)
{
    //Cla1ForceTask1(); // Use the CLA to do the control

    //
    // Clear ADCINT1 flag reinitialize for next SOC
    //
    AdcRegs.ADCINTFLGCLR.bit.ADCINT1 = 1;

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;   // Acknowledge interrupt to PIE
}

// cpu_timer0_isr, 1 kHz
__interrupt void cpu_timer0_isr(void)
{
    CpuTimer0.InterruptCount++;

    // Acknowledge this interrupt to receive more interrupts from group 1
    PieCtrlRegs.PIEACK.all |= PIEACK_GROUP1;
}

// Motor encoder ISR, update the motor position and speed, 1 kHz
__interrupt void ecap1_isr(void){

    Uint32 ecap1_t1 = ECap1Regs.CAP1;
    Uint32 ecap1_t2 = ECap1Regs.CAP2;
    float duty_count = ((float)ecap1_t1 / (float)ecap1_t2)*4119;

    if(duty_count > 15 && duty_count < 4112){ // if this is a valid frame

        Lincoln_Auto.motor_position_pre = Lincoln_Auto.motor_position;

        Lincoln_Auto.motor_position = duty_count - 16;

        Lincoln_Auto.motor_speed_pre = Lincoln_Auto.motor_speed;

        if( abs(Lincoln_Auto.motor_position - Lincoln_Auto.motor_position_pre ) < 2000)
        {
            Lincoln_Auto.motor_speed = Lincoln_Auto.motor_position_pre - Lincoln_Auto.motor_position;
        }
        else
        {
            if(Lincoln_Auto.motor_position_pre < 2000)
            { // car is moving forward
                Lincoln_Auto.motor_speed = Lincoln_Auto.motor_position_pre + 4096 - Lincoln_Auto.motor_position;
            }
            else{
                Lincoln_Auto.motor_speed = Lincoln_Auto.motor_position_pre - 4096 - Lincoln_Auto.motor_position;
            }
        }
        Lincoln_Auto.motor_speed_for_Jetson += Lincoln_Auto.motor_speed;

        Lincoln_Auto.motor_speed = Lincoln_Auto.motor_speed_pre*0.3 + 0.7*Lincoln_Auto.motor_speed;
    }
    else{
        Lincoln_Auto.motor_position_pre = Lincoln_Auto.motor_position;
        Lincoln_Auto.motor_position = Lincoln_Auto.motor_position + Lincoln_Auto.motor_speed;
    }

    // First 4 periods are for initialization. Zero the speed.
    if(count_init<4){
        count_init++;
        Lincoln_Auto.motor_speed = 0;
    }

    // Flash the LED2 for motor
    if(LED_Motor_Counter > 100){
        LED_Motor_Counter = 0;
        GpioDataRegs.GPBTOGGLE.bit.GPIO57 = 1;
    }
    else{
        LED_Motor_Counter++;
    }

    ECap1Regs.ECCLR.bit.CEVT2 = 1;
    ECap1Regs.ECCLR.bit.INT = 1;

    //
    // Acknowledge this interrupt to receive more interrupts from group 4
    //
    PieCtrlRegs.PIEACK.all |= PIEACK_GROUP4;
}

// Gimbal encoder ISR, update the gimbal position, 1 kHz
__interrupt void ecap3_isr(void){

    Uint32 ecap3_t1 = ECap3Regs.CAP1;
    Uint32 ecap3_t2 = ECap3Regs.CAP2;
    float duty_count = ((float)ecap3_t1 / (float)ecap3_t2)*4119;

    if(duty_count > 15 && duty_count < 4112){

        Lincoln_Auto.gimbal_position_pre = Lincoln_Auto.gimbal_position;

        Lincoln_Auto.gimbal_position = duty_count - 16;
    }

    // Flash the LED1 for gimbal
    if(LED_Gimbal_Counter > 100){
        LED_Gimbal_Counter = 0;
        GpioDataRegs.GPBTOGGLE.bit.GPIO56 = 1;
    }
    else{
        LED_Gimbal_Counter++;
    }

    ECap3Regs.ECCLR.bit.CEVT2 = 1;
    ECap3Regs.ECCLR.bit.INT = 1;

    //
    // Acknowledge this interrupt to receive more interrupts from group 4
    //
    PieCtrlRegs.PIEACK.all |= PIEACK_GROUP4;

}


// CAN Bus interrupt, suppose to be 100 Hz
__interrupt void ecan0_isr(void)
{
    ECanaShadow = ECanaRegs;

    if(ECanaShadow.CANGIF0.bit.GMIF0 == 1){
        ECanaShadow.CANTA.all = 0;
        ECanaShadow.CANTA.bit.TA0 = 1;       // Clear TA0
        ECanaRegs.CANTA.all = ECanaShadow.CANTA.all;


        // mailbox interrupt
        if(ECanaShadow.CANGIF0.bit.MIV0 == 0){
            // if it is TX (mailbox 0) interrupt
            ECanaShadow.CANTA.all = 0;
            ECanaShadow.CANTA.bit.TA0 = 1;       // Clear TA0
            ECanaRegs.CANTA.all = ECanaShadow.CANTA.all;

        }
        else if(ECanaShadow.CANGIF0.bit.MIV0 == 16){

            // if it is RX (mailbox 16) interrupt
            can_ReadMailBox(16, &Message_RX_L, &Message_RX_H);

            Message_RX_Index = Message_RX_L >> 16;
//            Lincoln_Auto.shadow_motor_speed = Message_RX_L & 0x0000ffff;

            int tmp_gimbal_pos = Message_RX_H >> 16;
            if(tmp_gimbal_pos < GIMBAL_POS_MIN){
                Lincoln_Auto.shadow_gimbal_position = GIMBAL_POS_MIN;
            }
            else if(tmp_gimbal_pos > GIMBAL_POS_MAX){
                Lincoln_Auto.shadow_gimbal_position = GIMBAL_POS_MAX;
            }
            else{
                Lincoln_Auto.shadow_gimbal_position = tmp_gimbal_pos;
            }

//            Lincoln_Auto.shadow_servo_position = Message_RX_H & 0x0000ffff;

            Message_TX_L  = (Message_RX_L & 0xffff0000) | (int) Lincoln_Auto.motor_speed_for_Jetson;
            Message_TX_H  = ((Uint32) Lincoln_Auto.gimbal_position) << 16 | Lincoln_Auto.battery_voltage_uint;

            Message_RX_Count++;
            Lincoln_Auto.motor_speed_for_Jetson = 0;

            // clear (RMP) Received-Message-Pending Register
            ECanaShadow.CANRMP.all = 0;
            ECanaShadow.CANRMP.bit.RMP16 = 1;
            ECanaRegs.CANRMP.all = ECanaShadow.CANRMP.all;

            can_SendMailBox(0, Message_TX_L, Message_TX_H); // send mailbox0
        }
    }
    else{
        // other interrupt
//        if(ECanaShadow.CANGIF0.bit.WLIF0 ){
//            ECanaShadow.CANGIF0.all = 0;
//            ECanaShadow.CANGIF0.bit.WLIF0 = 1;
//        }
//        else if(ECanaShadow.CANGIF0.bit.EPIF0){
//            ECanaShadow.CANGIF0.all = 0;
//            ECanaShadow.CANGIF0.bit.EPIF0 = 1;
//        }
//        else if(ECanaShadow.CANGIF0.bit.BOIF0){
//            ECanaShadow.CANGIF0.all = 0;
//            ECanaShadow.CANGIF0.bit.BOIF0 = 1;
//        }
//        else if(ECanaShadow.CANGIF0.bit.AAIF0){
//            ECanaShadow.CANGIF0.all = 0;
//            ECanaShadow.CANGIF0.bit.AAIF0 = 1;
//        }
//        else if(ECanaShadow.CANGIF0.bit.WDIF0){
//            ECanaShadow.CANGIF0.all = 0;
//            ECanaShadow.CANGIF0.bit.WDIF0 = 1;
//        }
    }

    // Flash the LED3 for CAN Bus
    if(LED_CANBus_Counter > 5){
        LED_CANBus_Counter = 0;
        GpioDataRegs.GPBTOGGLE.bit.GPIO58 = 1;
    }
    else{
        LED_CANBus_Counter++;
    }

    ECanaRegs.CANGIF0.all = ECanaShadow.CANGIF0.all;
    ECanaShadow_post = ECanaRegs;

    PieCtrlRegs.PIEACK.all |= PIEACK_GROUP9;
    return;
}

//
// End of File
//

