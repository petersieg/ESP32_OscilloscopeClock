/*

 *  Funktionsgenerator für Sinus, Dreieck und Rechteck Signale
 *  Einstellbare Frequenz 20 Hz bis 20 KHz
 *  Für Dreieck und Rechteck einstellbares Tastverhältnis 0 bis 100%
 *  Ausgangsspannung 3.3V Signale nur positiv!
*/

//Bibliotheken zum direkten Zugriff auf Steuerregister des ESP32
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "soc/rtc.h"

//Bibliotheken zur Verwendung des Digital zu Analog Konverters und für den I2S-Bus
#include "driver/dac.h"
#include "driver/i2s.h"

#define SINFAKT 127.0 //gemessen für Schrittweite = 1 und kein Vorteiler (8.3MHz)

//Buffer zum Erstellen der Dreieckfunktion
uint32_t buf[128];

//Einstellwerte für Kurvenform, Frequenz und Tastverhältnis
char mode = 'S'; //S=Sinus, R=Rechteck, T=Dreieck
float frequency = 1000; //20 bis 200000 Hz
uint8_t ratio = 50; //Tastverhältnis 0 bis 100%

//Flag Ist wahr, wenn die Initialisierung bereits erfolgte
bool initDone = false;

//Konfiguration für den I2S Bus
i2s_config_t i2s_config = {
     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN), //Betriebsart
     .sample_rate = 100000, //Abtastrate
     .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, // der DAC verwendet nur 8 Bit des MSB
     .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Kanalformat ESP32 unterstützt nur Stereo
     .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_I2S_MSB, //Standard Format für I2S
     .intr_alloc_flags = 0, // Standard Interrupt 
     .dma_buf_count = 2, //Anzahl der FIFO Buffer
     .dma_buf_len = 32, //Größe der FIFO Buffer
     .use_apll = 0 //Taktquelle
    };


//Buffer für Dreieck Wellenform füllen
//Parameter up ist die Dauer für den Anstieg in Prozent
//Parameter sz gibt die Buffergröße für eine Periode an
//es werden die Werte für eine Periode in den Buffer geschrieben
void fillBuffer(uint8_t up, uint8_t sz) {
  uint8_t down;  //Zeit für die fallende Flanke in %
  uint32_t sample; //32Bit Datenwort (I2S benötigt zwei Kanäle mit je 16 Bit
  float du,dd,val; //Hilfsvariablen
  down=100-up;
  //Anzahl der Schritte für Anstieg und Abfall berechnen
  uint16_t stup = round(1.0*sz/100 * up);
  uint16_t stdwn = round(1.0*sz/100*down);
  uint16_t i;
  if ((stup + stdwn) < sz) stup++;//Ausgleich eventueller Rundungsfehler
  //Amplitudenänderung pro Schritt für Anstieg und Abfall 
  du = 256.0/stup;
  dd = 256.0/stdwn;
  //füllen des Buffers
  val = 0; //Anstieg beginnt mit 0
  for (i=0; i<stup; i++) {
    sample = val; 
    sample = sample << 8; //Byte in das höherwertige Byte verschieben 
    buf[i]=sample;
    val = val+du; //Wert erhöhen
  }
  val=255; //Abfallende Flanke beginnt mit Maximalwert
  //Rest wie bei der ansteigenden Flanke
  for (i=0; i<stdwn; i++) {
    sample = val;
    sample = sample << 8;
    buf[i+stup]=sample;
    val = val-dd;
  }
}


//Alle  Ausgänge stoppen
void stopAll(){
    ledcDetachPin(26); 
    i2s_driver_uninstall((i2s_port_t)0); 
    dac_output_disable(DAC_CHANNEL_2);
    dac_i2s_disable();
    initDone=false;
}

//Kurvenform Rechteck starten
//Pin 26 als Ausgang zuweisen
void startRectangle(){
    ledcAttachPin(26,1 );
    initDone=true;
}

//Frequenz für Rechteck setzen mit entsprechendem Tastverhältnis
void rectangleSetFrequency(double frequency,uint8_t ratio)
{
    ledcSetup(1,frequency,7); //Wir nutzen die LEDC Funktion mit 7 bit Auflösung
    ledcWrite(1,127.0*ratio/100);  //Berechnung der Schrittanzahl für Zustand = 1
}


//Dreiecksignal starten
void startTriangle(){
  i2s_set_pin((i2s_port_t)0, NULL); //I2S wird mit dem DAC genutzt
    initDone=true;
}

//Frequenz für Dreieck setzen mit entsprechendem Tastverhältnis
double triangleSetFrequency(double frequency,uint8_t ratio)
{
  size_t dummy[1];
  
  int size=64;
  //zuerst wird die geeignete Buffergröße ermittelt
  //damit die Ausgabe funktionier muss die I2S Abtastrate zwischen
  //5200 und 650000 liegen
  if (frequency<5000) {
    size = 64;
  } else if (frequency<10000) {
    size = 32;
  } else if (frequency<20000) {
    size = 16;
  } else {
    size = 8;
  }
  //Abtastrate muss in einer Periode beide Buffer ausgeben
  uint32_t rate = frequency * 2 * size;
  //Die Abtastrate darf nur innerhalb der Grenzwerte liegen
  if (rate < 5200) rate = 5200;
  if (rate > 650000) rate = 650000;
  //wirklichen Frequenzwert setzen
  frequency = rate / 2 / size;

  //I2S Treiber entfernen 
  i2s_driver_uninstall((i2s_port_t)0);
  //Konfiguration anpassen 
  i2s_config.sample_rate = rate;
  i2s_config.dma_buf_len = size;
  //und mit der neuen Konfiguration installieren
  i2s_driver_install((i2s_port_t)0, &i2s_config, 0, NULL);
  //Abtastrate einstellen
  i2s_set_sample_rates((i2s_port_t)0, rate); 
  //Buffer füllen
  fillBuffer(ratio,size*2);
  //und einmal ausgeben
  i2s_write((i2s_port_t)0, (const char *)&buf, size*8, dummy, 100);  // esp32 V 2.0.2+
  //i2s_write_bytes((i2s_port_t)0, (const char *)&buf, size*8, 100);  // esp32 V 1.0.6
  return frequency;
}

//Sinusausgabe vorbereiten
void startSinus(){
    //Ausgang für Pin26 freigeben
    dac_output_enable(DAC_CHANNEL_2);
    // Sinusgenerator aktivieren
    SET_PERI_REG_MASK(SENS_SAR_DAC_CTRL1_REG, SENS_SW_TONE_EN);
    // Ausgabe auf Kanal 1 starten
    SET_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN2_M);
    // Vorzeichenbit umkehren
    SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_INV2, 2, SENS_DAC_INV2_S);
    initDone=true;
}

//Frequenz für Sinus setzen
double sinusSetFrequency(double frequency)
{
  //Formel f = s * SINFAKT / v
  //s sind die Schritte pro Taktimpuls
  //v ist der Vorteiler für den 8MHz Takt
  //Es gibt 8 Vorteiler von 1 bis 1/8 um die Kombination Vorteiler und
  //Schrittanzahl zu finden, testen wir alle acht Vorteiler Varianten
  //Die Kombination mit der geringsten Frequenzabweichung wird gewählt
  
    double f,delta,delta_min = 999999999.0;
    uint16_t divi=0, step=1, s;
    uint8_t clk_8m_div = 0;//0 bis 7
    for (uint8_t div = 1; div<9; div++){
      s=round(frequency * div/SINFAKT);
      if ((s>0) && ((div == 1) || (s<1024))) {
        f= SINFAKT*s/div;
        /*
        Serial.print(f); Serial.print(" ");
        Serial.print(div); Serial.print(" ");
        Serial.println(s);
        */
        delta = abs(f-frequency);
        if (delta < delta_min) { //Abweichung geringer -> aktuelle Werte merken
          step = s; divi = div-1; delta_min = delta;
        }
      }
    }
    //wirklichen Frequenzwert setzen
    frequency = SINFAKT * step / (divi+1);
    // Vorteiler einstellen
    REG_SET_FIELD(RTC_CNTL_CLK_CONF_REG, RTC_CNTL_CK8M_DIV_SEL, divi);
    // Schritte pro Taktimpuls einstellen
    SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL1_REG, SENS_SW_FSTEP, step, SENS_SW_FSTEP_S);
    return frequency;
}

//Einstellungsänderungen durchführen
void controlGenerator() {
  switch (mode) {
    case 'S' :
    case 's': if (!initDone) startSinus();
        frequency = sinusSetFrequency(frequency);
        break;
    case 'T' :
    case 't' : if (!initDone) startTriangle();
        frequency = triangleSetFrequency(frequency,ratio);
        break;
    case 'R' :
    case 'r' : if (!initDone) startRectangle();
        rectangleSetFrequency(frequency,ratio);
        break;
  }
}

//Serielle Schnittstelle aktivieren und 
//Defaulteinstellungen 1kHz Sinus setzen
void setup()
{
    Serial.begin(115200);
    controlGenerator();
    Serial.print("Kommando M,F,R : ");
}


void loop(){
  //Serielle Schnittstelle abfragen
  if (Serial.available() > 0) {
    //Befehl von der Schnittstelle einlesen
    String inp = Serial.readStringUntil('\n');
    //und zur Kontrolle ausgeben
    Serial.println(inp);
    char cmd = inp[0]; //erstes Zeichen ist das Kommando 
    if ((cmd == 'M') || (cmd == 'm')) { //war das Zeichen 'M' wird die Betriebsart eingestellt
      char newMode = inp[1]; //zweites Zeichen ist die Betriebsart
      if (newMode != mode) { //Nur wenn eine Änderung vorliegt, mus was getan werden
        stopAll(); 
        mode=newMode;
        controlGenerator();
      }
    } else {
      //bei den anderen Befehlen folgt ein Zahlenwert
      String dat = inp.substring(1);
      //je nach Befehl, werden die Daten geändert
      switch (cmd) {
        case 'F' :
        case 'f' :frequency = dat.toDouble(); break; //Frequenz
        case 'R' :
        case 'r' :ratio = dat.toInt(); break;  //Tastverhältnis
      }
      //Grenzwerte werden überprüft
      if (ratio > 100) ratio = 100;
      if (frequency < 20) frequency = 20;
      if (frequency > 20000) frequency = 20000;
      controlGenerator();
    }
    //aktuelle Werte ausgeben
    String ba;
    switch (mode) {
      case 'S':
      case 's': ba="Sinus"; break;
      case 'T':
      case 't': ba="Dreieck"; break;
      case 'R':
      case 'r': ba="Rechteck"; break;
    }
    Serial.println("**************** Eingestellte Werte *************************");
    Serial.print("Betriebsart    = "); Serial.println(ba);
    Serial.print("Frequenz       = "); Serial.print(frequency); Serial.println("Hz");
    Serial.print("Tastverhältnis = "); Serial.print(ratio); Serial.println("%");
    Serial.println();
    Serial.print("Kommando M,F,R : ");
  }
}
