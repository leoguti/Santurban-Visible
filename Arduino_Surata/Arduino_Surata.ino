#include "Adafruit_FONA.h"
#define FONA_RX 2
#define FONA_TX 9
#define FONA_RST 4
#define FONA_KEY 8
#define FONA_PS A1
#define HOURS_MED 0 
//DESCOMENTAR PARA MOVISTAR 
//#define APN "internet.movistar.com.co"
//#define APN_USER "movistar"
//#define APN_PASS "movistar"
//DESCOMENTAR PARA COMCEL 
#define APN "internet.comcel.com.co"
#define APN_USER "comcel"
#define APN_PASS "comcel"
//-------------------------------------
#define NUM_CEL "85870"
#define PRE_URL "api.pushingbox.com/pushingbox?devid=v6ADB65522726385&"
// this is a large buffer for replies


char replybuffer2[255];

//variables having the information that will be sent
char *lonChar;
char *latChar;
char *dateChar;
char *timeChar;
char tempChar[10];
char conductChar[10];
char battChar[10];
char imei[15] = {0}; // MUST use a 16 character buffer for IMEI!

char rChar[10] = {0}; // MUST use a 16 character buffer for IMEI!
char *simId;

bool OKUN=true;

// We default to using software serial. If you want to use hardware serial
// (because softserial isnt supported) comment out the following three lines 
// and uncomment the HardwareSerial line
#include <SoftwareSerial.h>
SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

// Hardware serial is also possible!
//HardwareSerial *fonaSerial = &Serial1;

// Use this for FONA 800 and 808s
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);
// Use this one for FONA 3G
//Adafruit_FONA_3G fona = Adafruit_FONA_3G(FONA_RST);

//uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);

uint8_t type;
uint8_t returncode;

//variables for conductivity calculations 
long pulseCount = 0;  //a pulse counter variable
unsigned long pulseTime,lastTime, duration, totalDuration;
int interruptPin = 1; //corresponds to D2
int samplingPeriod=3; // the number of seconds to measure 555 oscillations


//thermistor stuff -----------------------
#define THERMISTORPIN A0         
// resistance at 25 degrees C
#define THERMISTORNOMINAL 10000      
// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 25   
// how many samples to take and average, more takes longer
// but is more 'smooth'
#define NUMSAMPLES 5
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3950
// the value of the 'other' resistor
#define SERIESRESISTOR 10000    



void setup() {
  while (!Serial);
  //this pin power off/on the fona 
  pinMode(FONA_KEY,OUTPUT);
  //this pin habilite the PS this pin show the power fona status 
  pinMode(FONA_PS,INPUT);
  Serial.begin(115200);
}

void loop() {
  conductivity();
  temperature();
  start_fona(); 
  if (OKUN) get_time_loc();
  if (OKUN) send_http_get();     
  power_off_fona();
  error_led(10);
  delay(3600000*HOURS_MED);
}


void conductivity(){
  // conductivity --------------------------------------
  
  pulseCount=0; //reset the pulse counter
  totalDuration=0;  //reset the totalDuration of all pulses measured
  
  attachInterrupt(interruptPin,onPulse,RISING); //attach an interrupt counter to interrupt pin 1 (digital pin #3) -- the only other possible pin on the 328p is interrupt pin #0 (digital pin #2)
  
  pulseTime=micros(); // start the stopwatch
  
  delay(samplingPeriod*1000); //give ourselves samplingPeriod seconds to make this measurement, during which the "onPulse" function will count up all the pulses, and sum the total time they took as 'totalDuration' 
  
  detachInterrupt(interruptPin); //we've finished sampling, so detach the interrupt function -- don't count any more pulses

  float freqHertz;
  if (pulseCount>0) { //use this logic in case something went wrong
  
  double durationS=(totalDuration/double(pulseCount))/1000000.; //the total duration, in seconds, per pulse (note that totalDuration was in microseconds)

  freqHertz=1./durationS;
  }
  else {
    freqHertz=0.;
  }
  dtostrf(freqHertz,3,2,conductChar);

  //Serial.println(F("Conductividad"));
  //Serial.println(conductChar);

}

void onPulse()  //ver si esta en COnd
{
  pulseCount++;
  //Serial.print(F("pulsecount="));
  //Serial.println(pulseCount);
  lastTime = pulseTime;
  pulseTime = micros();
  duration=pulseTime-lastTime;
  totalDuration+=duration;
  //Serial.println(totalDuration);
}


void temperature(){
    uint8_t i;
    float average;
    int samples[NUMSAMPLES];
    // take N samples in a row, with a slight delay
    for (i=0; i< NUMSAMPLES; i++) {
      samples[i] = analogRead(THERMISTORPIN);
      delay(10);
    }
    // average all the samples out
    average = 0;
    for (i=0; i< NUMSAMPLES; i++) {
      average += samples[i];
    }
    average /= NUMSAMPLES;
    
    // convert the value to resistance
    average = 1023 / average - 1;
    average = SERIESRESISTOR / average;
    
    float steinhart;
    steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
    steinhart = log(steinhart);                  // ln(R/Ro)
    steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
    steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
    steinhart = 1.0 / steinhart;                 // Invert
    steinhart -= 273.15;                         // convert to C
    
    dtostrf(steinhart, 3, 2, tempChar);
    
    //Serial.println(F("Temperatura"));
    //Serial.println(tempChar);
  }

void restart_gprs(){
  delay(100);
  // Restore the gprs fona to be sure that up is up. 
  if (!fona.enableGPRS(false))
    Serial.println(F("Failed to turn off"));
  //Activate the GPRS  -- is needed for functions of time and position 
  if (!fona.enableGPRS(true))
    Serial.println(F("Failed to turn on"));
  //fona.setHTTPSRedirect(true);
}

void get_time_loc(){
  char replybuffer[255];  
  //return the GSM time an location 
  uint16_t returncode;
  if (!fona.getGSMLoc(&returncode, replybuffer, 250)){
    Serial.println(F("Failed!"));
    OKUN=false;
  }
    //falla 2 no hay red gprs
  if (returncode == 0) {
    Serial.println(replybuffer);
  } else {
    Serial.print(F("Fail code #")); Serial.println(returncode);
    OKUN=false;
  }
  lonChar = strtok(replybuffer, ",");
  latChar = strtok(NULL,",");
  dateChar = strtok(NULL,",");
  timeChar = strtok(NULL,",");
 
}

void power_off_fona()
{
  
  if (digitalRead(FONA_PS)==1) {
    digitalWrite(8, HIGH);delay(100);
    digitalWrite(8,LOW);delay(2000);
    digitalWrite(8,HIGH);delay(5000);
    Serial.println(F("Fona Apagado"));
  }
   
}


void power_on_fona()
{
  
  if (digitalRead(FONA_PS)==0) {
    digitalWrite(8, HIGH);delay(100);
    digitalWrite(8,LOW);delay(2000);
    digitalWrite(8,HIGH);delay(5000);   
    Serial.println(F("Fona Encendido"));
  }
   
}

void start_fona(){
  power_on_fona();

  Serial.println(F("FONA basic test"));
  Serial.println(F("Initializing....(May take 3 seconds)"));


  fonaSerial->begin(4800);
  if (! fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't find FONA"));
    OKUN=false;
    error_led(1);
  }
  OKUN=true;
  delay(5000);
  fona.setGPRSNetworkSettings(F(APN), F(APN_USER), F(APN_PASS));
  delay(5000);
  restart_gprs();
  delay(5000);
  per_bat(); 
  rssi();       

  // Print module IMEI number.
  uint8_t imeiLen = fona.getIMEI(imei);
  if (imeiLen > 0) {
    Serial.print(F("Module IMEI: ")); Serial.println(imei);
  }
  ccid();

}

void per_bat(){
  // read the battery voltage and percentage
  uint16_t vbat;
  if (! fona.getBattVoltage(&vbat)) {
    Serial.println(F("Failed to read Batt"));
    OKUN=false;
    //falla 3 no puedo encontrar la infromación de la bateria 
  } else {
    Serial.print(F("VBat = ")); Serial.print(vbat); Serial.println(F(" mV"));
  }
  if (! fona.getBattPercent(&vbat)) {
    Serial.println(F("Failed to read Batt"));
  } else {
    Serial.print(F("VPct = ")); Serial.print(vbat); Serial.println(F("%"));
    dtostrf(vbat, 3, 2, battChar);
    Serial.println(F("Per Batterie"));
    Serial.println(battChar);
  }
}



void send_sms(){
    // send an SMS!
    char message[141];
    char sendto[10]=NUM_CEL;
    Serial.println(F("Numero de celular:"));
    Serial.println(sendto);

    snprintf(message,160, "lonChar=%s&latChar=%s&dateChar=%s&timeChar=%s&tempChar=%s&conductChar=%s&battChar=%s&imei=%s&simId=%s&rChar=%s",
      lonChar,
      latChar,
      dateChar,
      timeChar,
      tempChar,
      conductChar,
      battChar,
      imei,
      simId,
      rChar      
    ); 
    Serial.println(F("Mensaje para envio: "));
    Serial.println(message);
    Serial.println(F("Numero de celular envío:"));
    Serial.println(sendto);
    if (!fona.sendSMS(sendto, message)) {
      Serial.println(F("Failed"));
      OKUN=false;
      //falla 4 no puedo enviar SMS
    } else {
      Serial.println(F("Sent!"));
    }
   delay(10000);
  }


void rssi() {
  // read the RSSI
  uint8_t n = fona.getRSSI();; //Signal leve RSSI
  int8_t r; //Signal level dBm

  Serial.print(F("RSSI = ")); Serial.print(n); Serial.print(": ");
  if (n == 0) r = -115;
  if (n == 1) r = -111;
  if (n == 31) r = -52;
  if ((n >= 2) && (n <= 30)) {
    r = map(n, 2, 30, -110, -54);
  }
  dtostrf(r, 3, 2, rChar);

  Serial.print(rChar); Serial.println(F(" dBm"));

}
  

void ccid(){
  // read the CCID
  fona.getSIMCCID(replybuffer2);  // make sure replybuffer is at least 21 bytes!
  simId = strtok(replybuffer2, ",");
  Serial.print(F("SIM CCID = ")); Serial.println(simId);  
  //delay(3000);
}


void send_http_get(){

  // send an SMS!
  char message[100]="";
  
  uint16_t statuscode;
  int16_t length;

  char str[350];
  
  strcpy (str,PRE_URL);
  strcat (str,"lonChar=");
  strcat (str,lonChar);
  strcat (str,"&latChar=");
  strcat (str,latChar);
  strcat (str,"&dateChar=");
  strcat (str,dateChar);
  strcat (str,"%20");
  strcat (str,timeChar);
  strcat (str,"&timeChar=");
  strcat (str,timeChar);
  strcat (str,"&tempChar=");
  strcat (str,tempChar);
  strcat (str,"&conductChar=");
  strcat (str,conductChar);
  strcat (str,"&battChar=");
  strcat (str,battChar);
  strcat (str,"&imei=");
  strcat (str,imei); 
  strcat (str,"&simId=");
  strcat (str,simId);
  strcat (str,"&rChar=");
  strcat (str,rChar);

  Serial.println(F("Mensaje para envio: "));
  Serial.println(str);
  Serial.println(F("fin Mensaje para envio: "));


  if (!fona.HTTP_GET_start(str, &statuscode, (uint16_t *)&length)) {
    Serial.println("Failed!");
  }

}

void error_led(int cod_error){
  int j;
  for (j=1;j<=cod_error;j++){
    digitalWrite(LED_BUILTIN,HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN,LOW);
    delay(500);
  }  
}
