#include <driver/gpio.h>  // for gpio_num_t declaration
#include <driver/dac.h>
#include <soc/rtc.h>
#include <soc/sens_reg.h>
#include <Arduino.h>

//the code in the comment is the original DAC output code
//there is some overhead to enter the mutex, disable the tone generator, and safe the other bits of the register
//this is taken out the inner loop of our code by doing this stuff just unce using prepare and unprepare
//the functions are inline, so there is no overhead of a function call
//the mutex can be ignored if no other task is messing around with the DAC registers. the compiler will optimize the mutex away then

extern portMUX_TYPE rtc_spinlock;
int cleanRegDAC1;
int cleanRegDAC2;

void inline DACPrepare(const bool useMutex)
{
  if(useMutex)
    portENTER_CRITICAL(&rtc_spinlock);
  CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL1_REG, SENS_SW_TONE_EN);
  CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN1_M);
  CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN2_M);
  SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, 255, RTC_IO_PDAC1_DAC_S);
  cleanRegDAC1 = (READ_PERI_REG(RTC_IO_PAD_DAC1_REG)&(~((RTC_IO_PDAC1_DAC)<<(RTC_IO_PDAC1_DAC_S))));
  cleanRegDAC2 = (READ_PERI_REG(RTC_IO_PAD_DAC2_REG)&(~((RTC_IO_PDAC2_DAC)<<(RTC_IO_PDAC2_DAC_S))));
}

void inline DAC1Write(const int value)
{
  WRITE_PERI_REG(RTC_IO_PAD_DAC1_REG, cleanRegDAC1 | ((value & RTC_IO_PDAC1_DAC) << RTC_IO_PDAC1_DAC_S));
}

void inline DAC2Write(const int value)
{
  WRITE_PERI_REG(RTC_IO_PAD_DAC2_REG, cleanRegDAC2 | ((value & RTC_IO_PDAC2_DAC) << RTC_IO_PDAC2_DAC_S));  
}

void inline DACUnprepare(const bool useMutex)
{
  if(useMutex)
    portEXIT_CRITICAL(&rtc_spinlock);
}

enum{b1=15, b2=13, b3=12, b4=14};
//enum{CuL=2, CuR=0, CuU=4, CuD=5};
//enum{b1=21, b2=13, b3=22, b4=14};
enum{CuL=16, CuR=17, CuU=18, CuD=19};
enum { _ALG_MAX_X = 33000, _ALG_MAX_Y = 41000 };

void DACInit(){
    //do once.
    dac_output_enable(DAC_CHANNEL_1);
    dac_output_enable(DAC_CHANNEL_2);

    pinMode(b1, INPUT_PULLUP);
    pinMode(b2, INPUT_PULLUP);
    pinMode(b3, INPUT_PULLUP);
    pinMode(b4, INPUT_PULLUP);

    pinMode(CuL, INPUT_PULLUP);
    pinMode(CuR, INPUT_PULLUP);
    pinMode(CuU, INPUT_PULLUP);
    pinMode(CuD, INPUT_PULLUP);
}
