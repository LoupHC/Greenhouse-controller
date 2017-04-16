#include <Arduino.h>

//*****************LIBRAIRIES************************
#include <EEPROM.h>
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include <DS3231.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TimeLord.h>

//*****************DEFINITIONS***********************
#define LCDKEYPAD A0
#define SAFETY_SWITCH A1
#define ONE_WIRE_BUS A2
#define menuPin 2
#define ROLLUP_OPEN 4
#define ROLLUP_CLOSE 5
const byte CHAUFFAGE[2] = {6,7}; // relais fournaise2
const byte FAN[2] = {8,9}; // relais fournaise2

#define HEURE 2
#define MINUTE 1
#define SR 1
#define CLOCK 2
#define SS 3
#define CLOSE LOW
#define OPEN HIGH
#define ON HIGH
#define OFF LOW

//******************EEPROM INDEX*********************
#define TIMEARRAY 3
#define PROGRAMS 15
#define SRMOD 20
#define SSMOD 25
#define TEMPCIBLE 30
#define RMOD 35
#define VMOD 37
#define HMOD 39
#define RHYST 41
#define VHYST 43
#define HHYST 45
#define RAMPING 47
#define INCREMENTS 48
#define ROTATION 49
#define PAUSE 50
//*********************OBJETS************************
//---------------------LCD-----------------
#define I2C_ADDR    0x27              // Define I2C Address where the PCF8574A is
#define BACKLIGHT_PIN     3
LiquidCrystal_I2C  lcd(I2C_ADDR, 2, 1, 0, 4, 5, 6, 7);
//---------------------RTC-----------------
DS3231  rtc(SDA, SCL);                // Init the DS3231 using the hardware interface
Time  t;
//--------------------DS18B20-------------
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
//-------------------Timelord-------------
const int TIMEZONE = -5; //PST
const float LATITUDE = 45.50, LONGITUDE = -73.56; // set your position here
TimeLord myLord; // TimeLord Object, Global variable

//*********************VARIABLES GLOBALES*************
//nombre d'items activés
const int nbPrograms = 5;              //Nombre de programmes de température
const int nbRollups = 1;               //Nombre de sorties de rollups
const int nbHeaters = 2;               //Nombre de sorties de fournaise
const int nbFans = 1;                  //Nombre de sorties de fan

boolean rollups[nbRollups];
boolean fans[nbHeaters];
boolean heaters[nbFans];
boolean safetySwitch[nbFans] = {true};

byte sunTime[6];                      //données de l'horloge
float greenhouseTemperature = 20;     //données de la sonde de température
boolean checkSensor = false;

//Initialisation
boolean firstOpening = true;

//Programmes horaire
int P[nbPrograms][3];                 //Heures de programmes horaires(max. 5)
byte program;                         //Programme en cour
byte lastProgram;                    //Dernier programme en cour

//Programmes de température
float tempCible;                     //Température cible
float tempRollup[nbRollups];         //Température d'activation des rollups (max.2)
float tempHeater[nbHeaters];         //Température d'activation des fournaises (max.2)
float tempFan[nbFans];                //Température d'activation des fans (max.2)

//Temps de rotation et de pause des moteurs
byte incrementCounter;               //positionnement des rollups

//Ramping
int newTempCible;                   //Température cible à atteindre
unsigned long lastCount = 0;         //Compteur

//Autres variables
const int sleeptime = 1000;           //Temps de pause entre chaque exécution du programme(en mili-secondes)

//-----------------------LCD Menu------------------
//menuPin
boolean menuPinState = 1;
//curseur
byte currentMenuItem = 1;
//buttonstate
byte state = 0;
byte laststate = 0;
//menu
int menu = 0;
//#items/menu
byte Nbitems = 6;
char* menuitems[10];
int yr; byte mt; byte dy; byte hr; byte mn; byte sc;


//**************************************************************
//*****************       SETUP      ***************************
//**************************************************************

void setup() {
  //Initialisation (moniteur série, horloge, capteurs, affichage, données astronomiques)

  Serial.begin(9600);
  rtc.begin();
  sensors.begin();
  uploadNewSettings();
  //Initialise l'affichage LCD
  initLCD();
  //Initialise la librairie TimeLord
  initTimeLord();
  //Première lecture d'horloge pour définir le lever et coucher du soleil
  getDateAndTime();
  //Définition du lever et du coucher du soleil
  setSunriseSunSet();
  //Lecture d'horloge véridique
  getDateAndTime();
  //Sélection du programme en cour
  selectProgram();
  //Définition de la température cible
  setTempCible();
  //Définition de la température d'activation des sorties
  setOutputsTempCible();
  //Actualise la température
  getTemperature();
  //Définition des I/Os
  initOutputs();

  setIOS();
  //Affichage LCD
  lcdDisplay();
}



//**************************************************************
//******************       LOOP      ***************************
//**************************************************************

void loop() {

  //MODE MENU
  if (digitalRead(menuPin) == LOW) {
    //Programme de menu
    Menu(menu);
    delay(50);
  }

  //MODE CONTROLE
  else if (digitalRead(menuPin) == HIGH) {
    //Protocole de controle
    //Ajuste l'heure de lever et coucher du soleil si la journée change
    checkSunriseSunset();
    //Vérifie si le programme de température doit être modifié
    selectProgram();
    //Définition de la température d'activation des sorties
    setOutputsTempCible();
    //Augmente progressivement la température cible (+0,5C tous les x minutes)
    startRamping();
    //Actualise la température
    getTemperature();
    //Protocoles spéciaux (pré-jour/pré-nuit)
    specialPrograms();
    //Affichage LCD
    lcdDisplay();
    //Activation des relais
    relayLogic();
    //Affichage LCD
    lcdDisplay();
    //Pause entre chaque cycle
    delay(sleeptime);
  }
}


//**************************************************************
//****************    MACROS - SETUP     ***********************
//**************************************************************

void uploadNewSettings(){

  //*********************VARIABLES EEPROM*************

  byte nbProgramsE[5] = {SR, SR, CLOCK, SS, SS};           //Type de programme (SR = basé sur lever du soleil, CLOCK = programmé manuellement, SS = basé sur le coucher du soleil, 0 = vide)
  byte progTime[5][3]={{0, 0, 0},
                       {0, 0, 0},
                       {0, 0, 11},                        //Heure des programmes en mode  manuel (seconde, minute, heure)]={
                       {0, 0, 0},
                       {0, 0, 0}};

  byte srmodE[5] = {40, 70, 0, 0, 0};                      //Décalage par rapport à l'heure du lever du soleil pour les programme en mode SR(0 = -1h, 120 = +1h)
  byte ssmodE[5] = {0, 0, 0, 40, 90};                      //Décalage par rapport à l'heure du lever du soleil pour les programme en mode SS(0 = -1h, 120 = +1h)
  //Températures cibles
  byte tempCibleE[5] = {20, 22, 24, 20, 18};               //Température cible selon les programmes
  //Modificateurs
  byte rmodE[2] = {10};                                    //Modificateur relais par rapport à la température cible (0 = -10, 10 = 0, 20 = +10)
  byte vmodE[2] = {12,13};                                    //Modificateur ventilation
  byte hmodE[2] = {8, 6};                                  //Modificateur fournaise 1 et 2
  //Hysteresis
  byte hystRollupE[2] = {1};                               //hysteresis rollup
  byte hystVentE[2] = {1, 1};                                 //hysteresis ventilation
  byte hystHeaterE[2] = {1,1};                             //hysteresis fournaise 1 et 2
  //temps de rotation et pauses
  //Ramping
  byte ramping = 5;                                        //Minutes de ramping
  byte increments = 5;
  byte rotation = 2;
  byte pause = 1;

  //update Programmable time
  for(byte x = 0 ; x < 5; x++){
    for(byte y = 0; y < TIMEARRAY; y++){
      EEPROM.update((x*TIMEARRAY+y), progTime[x][y]);
    }
  }
  //update nbProgramsE
  for(byte x = 0 ; x < sizeof(nbProgramsE); x++){
    switch (x){
      case 0:EEPROM.update((PROGRAMS+x), nbProgramsE[0]); break;
      case 1:EEPROM.update((PROGRAMS+x), nbProgramsE[1]); break;
      case 2:EEPROM.update((PROGRAMS+x), nbProgramsE[2]); break;
      case 3:EEPROM.update((PROGRAMS+x), nbProgramsE[3]); break;
      case 4:EEPROM.update((PROGRAMS+x), nbProgramsE[4]); break;
    }
  }
  //update srmodE
  for(byte x = 0 ; x < sizeof(srmodE); x++){
    switch (x){
      case 0:EEPROM.update((SRMOD+x), srmodE[0]); break;
      case 1:EEPROM.update((SRMOD+x), srmodE[1]); break;
      case 2:EEPROM.update((SRMOD+x), srmodE[2]); break;
      case 3:EEPROM.update((SRMOD+x), srmodE[3]); break;
      case 4:EEPROM.update((SRMOD+x), srmodE[4]); break;
    }
  }
  //update ssmodE
  for(byte x = 0 ; x < sizeof(ssmodE); x++){
    switch (x){
      case 0:EEPROM.update((SSMOD+x), ssmodE[0]); break;
      case 1:EEPROM.update((SSMOD+x), ssmodE[1]); break;
      case 2:EEPROM.update((SSMOD+x), ssmodE[2]); break;
      case 3:EEPROM.update((SSMOD+x), ssmodE[3]); break;
      case 4:EEPROM.update((SSMOD+x), ssmodE[4]); break;
    }
  }
  //update tempCibleE
  for(byte x = 0 ; x < sizeof(tempCibleE); x++){
    switch (x){
      case 0:EEPROM.update((TEMPCIBLE+x), tempCibleE[0]); break;
      case 1:EEPROM.update((TEMPCIBLE+x), tempCibleE[1]); break;
      case 2:EEPROM.update((TEMPCIBLE+x), tempCibleE[2]); break;
      case 3:EEPROM.update((TEMPCIBLE+x), tempCibleE[3]); break;
      case 4:EEPROM.update((TEMPCIBLE+x), tempCibleE[4]); break;
    }
  }
  //update modificateurs rollups
  for(byte x = 0 ; x < sizeof(rmodE); x++){
    switch (x){
      case 0:EEPROM.update((RMOD+x), rmodE[0]); break;
      case 1:EEPROM.update((RMOD+x), rmodE[1]); break;
    }
  }
  //update modificateurs chaufferette
  for(byte x = 0 ; x < sizeof(vmodE); x++){
    switch (x){
      case 0:EEPROM.update((VMOD+x), vmodE[0]); break;
      case 1:EEPROM.update((VMOD+x), vmodE[1]); break;
    }
  }
  //update modificateurs chaufferette
  for(byte x = 0 ; x < sizeof(hmodE); x++){
    switch (x){
      case 0:EEPROM.update((HMOD+x), hmodE[0]); break;
      case 1:EEPROM.update((HMOD+x), hmodE[1]); break;
    }
  }
  //update modificateurs chaufferette
  for(byte x = 0 ; x < sizeof(hystRollupE); x++){
    switch (x){
      case 0:EEPROM.update((RHYST+x), hystRollupE[0]); break;
      case 1:EEPROM.update((RHYST+x), hystRollupE[1]); break;
    }
  }
  for(byte x = 0 ; x < sizeof(hystVentE); x++){
    switch (x){
      case 0:EEPROM.update((VHYST+x), hystVentE[0]); break;
      case 1:EEPROM.update((VHYST+x), hystVentE[1]); break;
    }
  }
  for(byte x = 0 ; x < sizeof(hystHeaterE); x++){
    switch (x){
      case 0:EEPROM.update((HHYST+x), hystHeaterE[0]); break;
      case 1:EEPROM.update((HHYST+x), hystHeaterE[1]); break;
    }
  }
  EEPROM.update(RAMPING, ramping);
  EEPROM.update(INCREMENTS, increments);
  EEPROM.update(ROTATION, rotation);
  EEPROM.update(PAUSE, pause);
}

void initLCD(){
  lcd.begin(20, 4);
  lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);
  lcd.setBacklight(HIGH);
  lcd.clear();
}
void initTimeLord(){
  myLord.TimeZone(TIMEZONE * 60);
  myLord.Position(LATITUDE, LONGITUDE);
  myLord.DstRules(3,2,11,1,60); // DST Rules for USA
}


void setSunriseSunSet(){
  byte sunRise[6];
  byte sunSet[6];
  myLord.SunRise(sunTime); ///On détermine l'heure du lever du soleil
  myLord.DST(sunTime);//ajuster l'heure du lever en fonction du changement d'heure
  sunRise[HEURE] = sunTime[HEURE];
  sunRise[MINUTE] = sunTime[MINUTE];
  //Serial.print("lever du soleil :");Serial.print(sunRise[HEURE]);  Serial.print(":");  Serial.println(sunRise[MINUTE]);

  /* Sunset: */
  myLord.SunSet(sunTime); // Computes Sun Set. Prints:
  myLord.DST(sunTime);
  sunSet[HEURE] = sunTime[HEURE];
  sunSet[MINUTE] = sunTime[MINUTE];
  //Serial.print("coucher du soleil :");  Serial.print(sunSet[HEURE]);  Serial.print(":");  Serial.println(sunSet[MINUTE]);

  setPrograms(sunRise[HEURE], sunRise[MINUTE], sunSet[HEURE], sunSet[MINUTE]);
}

byte PROGRAM_TIME(byte counter, byte timeData){
  return EEPROM.read(TIMEARRAY*counter+timeData);
}

void setPrograms(byte HSR, byte MSR, byte HSS, byte MSS){
  //Définition des variables locales
  int srmod[nbPrograms];
  int ssmod[nbPrograms];
  byte programType[nbPrograms];

  for(byte x = 0; x < nbPrograms; x++){
    programType[x] = EEPROM.read(PROGRAMS+x);
    srmod[x] =  EEPROM.read(SRMOD+x)-60;
    ssmod[x] =  EEPROM.read(SSMOD+x)-60;
  }
  //Exécution du programme
  //Ajuste l'heure des programmes en fonction du lever et du coucher du soleil
  for(byte x = 0; x < nbPrograms; x++){
    //Serial.println(x);Serial.println (programType[x]);
    if (programType[x] == SR){
      P[x][HEURE] = HSR;
      P[x][MINUTE] = MSR + srmod[x];
      convertDecimalToTime(&P[x][HEURE], &P[x][MINUTE]);
      //Serial.print(" Program ");Serial.print(x);Serial.print(" : ");Serial.print(P[x][HEURE]);Serial.print(" : ");Serial.println(P[x][MINUTE]);
    }

    else if (programType[x] == CLOCK){
      P[x][HEURE] = PROGRAM_TIME(x, HEURE);
      P[x][MINUTE] = PROGRAM_TIME(x, MINUTE);
      //Serial.print(" Program ");Serial.print(x);Serial.print(" : ");Serial.print(P[x][HEURE]);Serial.print(" : ");Serial.println(P[x][MINUTE]);
    }

    else if (programType[x] == SS){
      P[x][HEURE] = HSS;
      P[x][MINUTE] = MSS + ssmod[x];
      convertDecimalToTime(&P[x][HEURE], &P[x][MINUTE]);
      //Serial.print(" Program ");Serial.print(x); Serial.print(" : "); Serial.print(P[x][HEURE]);Serial.print(" : ");Serial.println(P[x][MINUTE]);
    }
  }
}

void selectProgram(){
//Sélectionne le programme en cour
  //Serial.print ("Heure actuelle ");Serial.print(" : ");Serial.print(sunTime[HEURE] );Serial.print(" : ");Serial.println(sunTime[MINUTE]);
  for (byte y = 0; y < (nbPrograms-1); y++){
  //Serial.print ("Programme "); Serial.print(y+1);Serial.print(" : ");Serial.print(P[y][HEURE]);Serial.print(" : ");Serial.println(P[y][MINUTE]);
    if (((sunTime[HEURE] == P[y][HEURE])  && (sunTime[MINUTE] >= P[y][MINUTE]))||((sunTime[HEURE] > P[y][HEURE]) && (sunTime[HEURE] < P[y+1][HEURE]))||((sunTime[HEURE] == P[y+1][HEURE])  && (sunTime[MINUTE] < P[y+1][MINUTE]))){
      program = y+1;
      //Serial.println("YES!");
    }
  }
//  Serial.print ("Programme ");Serial.print(nbPrograms);Serial.print(" : ");Serial.print(P[nbPrograms-1][HEURE]);Serial.print(" : ");Serial.println(P[nbPrograms-1][MINUTE]);
  if (((sunTime[HEURE] == P[nbPrograms-1][HEURE])  && (sunTime[MINUTE] >= P[nbPrograms-1][MINUTE]))||(sunTime[HEURE] > P[nbPrograms-1][HEURE])||(sunTime[HEURE] < P[0][HEURE])||((sunTime[HEURE] == P[0][HEURE])  && (sunTime[MINUTE] < P[0][MINUTE]))){
    program = nbPrograms;
    //Serial.println("YES!");
  }
}

void setTempCible(){
  for (byte x = 0; x < nbPrograms; x++){
    if(program == x+1){
      tempCible = EEPROM.read(TEMPCIBLE+x);
    }
  }
}

void setOutputsTempCible(){
  //Définition des variables locales
  int rmod[nbRollups];
  int vmod[nbFans];
  int hmod[nbHeaters];

  for (int x = 0; x < nbRollups; x++){
    int rmodE = EEPROM.read(RMOD+x);
    rmod[x] = rmodE-10;
  }
  for (int x = 0; x < nbFans; x++){
    int vmodE = EEPROM.read(VMOD+x);
    vmod[x] = vmodE-10;
  }
  for (int x = 0; x < nbHeaters; x++){
    int hmodE = EEPROM.read(HMOD+x);
    hmod[x] = hmodE-10;
  }

  //Exécution du programme
  if(program != lastProgram){
    for(byte x = 0; x < nbRollups; x++){
      tempRollup[x] = tempCible + rmod[x];
      //Serial.print(F("temp Cible: "));Serial.println(tempCible) ;Serial.print(F("rmod"));Serial.print(x+1);Serial.print(": ");Serial.println(rmod[x]);Serial.print(F("temp rollup"));Serial.print(x+1);Serial.print(": ");Serial.println(tempRollup[x]);Serial.println(F(""));
    }
    for(byte x = 0; x < nbFans; x++){
      tempFan[x] = tempCible + (float)vmod[x];
      //Serial.print(F("temp Cible: "));Serial.println(tempCible) ;Serial.print(F("vmod"));Serial.print(x+1);Serial.print(F(": "));Serial.println(vmod[x]);Serial.print(F("temp fan"));Serial.print(x+1);Serial.print(": ");Serial.println(tempFan[x]);Serial.println(F(""));
    }
    for(byte x = 0; x < nbHeaters; x++){
      tempHeater[x] = tempCible + (float)hmod[x];
      //Serial.print(F("temp Cible: "));Serial.println(tempCible) ;Serial.print(F("hmod"));Serial.print(x+1);Serial.print(F(": "));Serial.println(hmod[x]);Serial.print(F("temp heater"));Serial.print(x+1);Serial.print(": ");Serial.println(tempHeater[x]);Serial.println(F(""));
    }
  lastProgram = program;
  }
}

void initOutputs(){
  //Activation des items
  for (int x = 0; x < nbRollups;x++){
    rollups[x] = true;
  }
  for (int x = 0; x < nbFans;x++){
    fans[x] = true;
  }
  for (int x = 0; x < nbHeaters;x++){
    heaters[x] = true;
  }
}

void setIOS(){
  //Definition des entrées
  pinMode(LCDKEYPAD, INPUT_PULLUP);
  pinMode(SAFETY_SWITCH, INPUT_PULLUP);
  pinMode(menuPin, INPUT_PULLUP);
  //Définition et initalisation des sorties
  pinMode(ROLLUP_OPEN, OUTPUT);
  digitalWrite(ROLLUP_OPEN, LOW);
  pinMode(ROLLUP_CLOSE, OUTPUT);
  digitalWrite(ROLLUP_CLOSE, LOW);
  pinMode(CHAUFFAGE[0], OUTPUT);
  digitalWrite(CHAUFFAGE[0], LOW);
  pinMode(CHAUFFAGE[1], OUTPUT);
  digitalWrite(CHAUFFAGE[1], LOW);
  pinMode(FAN[0], OUTPUT);
  digitalWrite(FAN[0], LOW);
  pinMode(FAN[1], OUTPUT);
  digitalWrite(FAN[1], LOW);
}

void lcdDisplay() {
  lcd.noBlink();
  //Si passe du mode menu au mode controle...
  if (menuPinState == 0) {
    lcdPrintTemp();
    lcdPrintTime();
    lcdPrintRollups();
    lcdPrintOutputsStatus();
    menuPinState = 1;
  }
  //Séquence normale...
  lcdPrintTemp();
  lcdPrintTime();
  lcdPrintRollups();
  lcdPrintOutputsStatus();
}



//**************************************************************
//****************    MACROS - CONTROLE     ********************
//**************************************************************

void checkSunriseSunset(){
  t = rtc.getTime();
  int actual_day = t.date;

  if (sunTime[3] != actual_day){
      setSunriseSunSet();
  }
  getDateAndTime();
}

//Programme de courbe de température
void startRamping(){
  //Définition des variables locales
  byte tempCibleE[nbPrograms];
  unsigned int RAMPING_INTERVAL;

  for (byte x = 0; x < nbPrograms; x++){
    tempCibleE[x] = EEPROM.read(TEMPCIBLE+x);
  }
  RAMPING_INTERVAL = EEPROM.read(RAMPING)*60*1000;

  //Exécution du programme
  for (byte x = 0; x < nbPrograms; x++){
    if(program == x+1){
      newTempCible = tempCibleE[x];
    }
  }

  if (newTempCible > tempCible){
    unsigned long rampingCounter = millis();
    if(rampingCounter - lastCount > RAMPING_INTERVAL) {
      lastCount = rampingCounter;
      tempCible += 0.5;
    }
  }
  else if (newTempCible < tempCible){
    unsigned long rampingCounter = millis();
    if(rampingCounter - lastCount > RAMPING_INTERVAL) {
      lastCount = rampingCounter;
      tempCible -= 0.5;
    }
  }
}

void getTemperature(){
  sensors.requestTemperatures();
  greenhouseTemperature = sensors.getTempCByIndex(0);

  if((greenhouseTemperature < -20.00)||(greenhouseTemperature > 80)){
    greenhouseTemperature = EEPROM.read(TEMPCIBLE+program-1)+2;
    checkSensor = true;
  }
  else{
    checkSensor = false;
  }
}


void specialPrograms(){}

void relayLogic(){
  //Définition des variables locales
  byte hystRollups[nbRollups];
  byte hystHeater[nbHeaters];
  byte hystFan[nbFans];

  for(byte x = 0; x < nbRollups; x++){
    hystRollups[x] = EEPROM.read(RHYST+x);
  }
  for(byte x = 0; x < nbHeaters; x++){
    hystHeater[x] = EEPROM.read(HHYST+x);
  }
  for(byte x = 0; x < nbRollups; x++){
    hystFan[x] = EEPROM.read(VHYST+x);
  }

  //Exécution du programme
  lcd.noBlink();
  //Programme d'ouverture/fermeture des rollups
  for(byte x = 0; x < nbRollups; x++){
    if (rollups[x] == true){
      if (greenhouseTemperature < (tempRollup[x] - hystRollups[x])) {
        closeSides();
      } else if (greenhouseTemperature > tempRollup[x]) {
        openSides();
      }
    }
  }

  //Programme fournaise
  for(byte x = 0; x < nbHeaters; x++){
    if (heaters[x] == true){

      if ((greenhouseTemperature < tempHeater[x])&&(incrementCounter == 0)) {
        digitalWrite(CHAUFFAGE[x], ON);
      } else if ((greenhouseTemperature > (tempHeater[x] + hystHeater[x]))||(incrementCounter != 0)) {
        digitalWrite(CHAUFFAGE[x], OFF);
      }
    }
  }

  //Programme ventilation forcée
  for(byte x = 0; x < nbFans; x++){
    if (fans[x] == true){
      if (safetySwitch[x] == true){
        if (greenhouseTemperature > tempFan[x]&&(incrementCounter == EEPROM.read(INCREMENTS))&&(digitalRead(SAFETY_SWITCH) == OFF)){
          digitalWrite(FAN[x], ON);
        }
        else if ((greenhouseTemperature < (tempFan[x] - hystFan[x]))||(digitalRead(SAFETY_SWITCH) == ON)) {
          digitalWrite(FAN[x], OFF);
        }
      }
      else if (safetySwitch[x] == false){
        if (greenhouseTemperature > tempFan[x]){
          digitalWrite(FAN[x], ON);
        }
        else if (greenhouseTemperature < (tempFan[x] - hystFan[x])) {
          digitalWrite(FAN[x], OFF);
        }
      }
    }
  }
}

//**************************************************************
//****************    MACROS - AUTRES     **********************
//**************************************************************

void getDateAndTime(){
  t = rtc.getTime();
  sunTime[5] = t.year-2000;
  sunTime[4] = t.mon;
  sunTime[3] = t.date;
  sunTime[HEURE] = t.hour;
  sunTime[MINUTE] = t.min;
  sunTime[0] = t.sec;
  myLord.DST(sunTime);
}

//Programme pour convertir l'addition de nombres décimales en format horaire
void convertDecimalToTime(int * heure, int * minut){
  //Serial.println(m);
  if ((*minut > 59) && (*minut < 120)){
    *heure += 1;
    *minut -= 60;
  }
  else if ((*minut < 0) && (*minut >= -60)){
    *heure -= 1;
    *minut +=60;
  }
}

//Programme d'ouverture des rollup
void openSides() {
  unsigned int pause = EEPROM.read(PAUSE) * 1000;

  if (firstOpening == true){
    incrementCounter = 0;
    firstOpening = false;
  }
  if (incrementCounter < EEPROM.read(INCREMENTS)) {
  incrementCounter += 1;
    lcd.setCursor(0, 1);
    lcd.print(F("OUVERTURE"));
    digitalWrite(ROLLUP_OPEN, ON);
    delay(EEPROM.read(ROTATION) * 1000);
    digitalWrite(ROLLUP_OPEN, OFF);
    lcd.setCursor(0, 1);
    lcd.print(F("R-U:     "));
    lcd.setCursor(5, 1);
    lcd.print(incrementCounter);
    delay(pause);
  }

}

//Programme de fermeture des rollups
void closeSides() {
  unsigned int pause = EEPROM.read(PAUSE) * 1000;

  if (firstOpening == true){
    incrementCounter = EEPROM.read(INCREMENTS);
    firstOpening = false;
  }
  if (incrementCounter > 0) {
    incrementCounter -= 1;
    lcd.setCursor(0, 1);
    lcd.print(F("FERMETURE"));
    digitalWrite(ROLLUP_CLOSE, ON);
    delay(EEPROM.read(ROTATION) * 1000);
    digitalWrite(ROLLUP_CLOSE, OFF);
    lcd.setCursor(0, 1);
    lcd.print(F("R-U:     "));
    lcd.setCursor(5, 1);
    lcd.print(incrementCounter);
    delay(pause);
  }
}


void lcdPrintRollups(){
    lcd.setCursor(0, 1); lcd.print(F("         "));
    lcd.setCursor(0, 1); lcd.print(F("R-U: "));
    lcd.setCursor(5, 1); lcd.print(incrementCounter);
 }
void lcdPrintTemp(){
    lcd.setCursor(0,0); lcd.print(F("                    "));
    if(checkSensor == false){
      lcd.setCursor(0,0); lcd.print(F("T:")); lcd.print(greenhouseTemperature); lcd.print(F("C |TC:"));
    }
    else{
      lcd.setCursor(0,0); lcd.print(F("T:")); lcd.print("!!!"); lcd.print(F("("));lcd.print((int)greenhouseTemperature);lcd.print(F(")|TC:"));
    }
    lcd.setCursor(13,0); lcd.print(tempCible); lcd.print(F("C"));
}
void lcdPrintTime(){
    lcd.setCursor(9,1); lcd.print(F("|(P")); lcd.print(program); lcd.print(F(":"));
    lcd.setCursor(14,1); lcdPrintDigits(sunTime[HEURE]); lcd.print(F(":")); lcdPrintDigits(sunTime[MINUTE]);
    lcd.setCursor(19,1); lcd.print(F(")"));
}

void lcdPrintOutputsStatus(){
  lcd.setCursor(0, 2); lcd.print(F("                    "));
  lcd.setCursor(0, 3); lcd.print(F("         "));

  if (fans[0] == true){
      lcd.setCursor(0, 3); lcd.print(F("FAN:"));
      if (digitalRead(FAN[0]) == OFF) {      lcd.setCursor(5, 3); lcd.print(F("OFF"));    }
      else if (digitalRead(FAN[0]) == ON) {      lcd.setCursor(5, 3); lcd.print(F("ON "));    }
    }

  if (heaters[0] == true){
    lcd.setCursor(0, 2); lcd.print(F("H1:"));
    if (digitalRead(CHAUFFAGE[0]) == OFF) {      lcd.setCursor(5, 2); lcd.print(F("OFF |"));    }
    else if (digitalRead(CHAUFFAGE[0]) == ON) {      lcd.setCursor(5, 2); lcd.print(F("ON  |"));    }
  }

  if (heaters[1] == true){
    lcd.setCursor(11, 2); lcd.print(F("H2:"));
    if (digitalRead(CHAUFFAGE[1]) == OFF) {      lcd.setCursor(16, 2); lcd.print(F("OFF"));    }
    else if (digitalRead(CHAUFFAGE[1]) == ON) {      lcd.setCursor(16, 2); lcd.print(F("ON "));    }
  }
}

void lcdPrintDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  if(digits < 10)
  lcd.print("0");
  lcd.print(digits);
}

void serialPrintDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  if(digits < 10)
  Serial.print("0");
}



//**************************************************************
//****************    MACROS - MENU     ***********************
//**************************************************************

void Menu(int x) {

  if (menuPinState == 1) {
    displayMenu(menu);
    menuPinState = 0;
  }

  int a = analogRead(A0);
  Serial.println(a);
  buttonState(a);

  if ((state == 3) && (state != laststate)) {
    currentMenuItem = currentMenuItem + 1;
    real_currentMenuItem(Nbitems);
    displayMenu(x);
  } else if ((state == 2) && (state != laststate)) {
    currentMenuItem = currentMenuItem - 1;
    real_currentMenuItem(Nbitems);
    displayMenu(x);
  } else if ((state == 1) && (state != laststate)) {
    real_currentMenuItem(Nbitems);
    selectMenu(x, currentMenuItem);
  }
  laststate = state;
}

void buttonState(int x) {
  if (x < 50) {
    state = 0;
  }
  else if (x < 80) {
    state = 1;
  }
  else if (x < 100) {
    state = 2;
  }
  else if (x < 200) {
    state = 3;
  }
  else {
    state = 0;
  }
}

void real_currentMenuItem(int x) {
  if (currentMenuItem < 1) {
    currentMenuItem = 1;
  }
  else if (currentMenuItem > x) {
    currentMenuItem = x;
  }
}

void Scrollingmenu (int x, const char a[20] PROGMEM, const char b[20] PROGMEM, const char c[20] PROGMEM, const char d[20] PROGMEM, const char e[20] PROGMEM, const char f[20] PROGMEM, const char g[20] PROGMEM, const char h[20] PROGMEM, const char i[20] PROGMEM, const char j[20] PROGMEM, int y) {
  const int numLcdRows = 3;
  byte scrollPos = 0;
  Nbitems = x;
  const char* menuitems[] PROGMEM = {a, b, c, d, e, f, g, h, i, j};

  if (y > numLcdRows) {
    scrollPos = y - numLcdRows;
  } else {
    scrollPos = 0;
  }
  clearPrintTitle();
  for (byte i = 0; i < numLcdRows; ++i) {
    lcd.setCursor(0, i + 1);
    lcd.print(menuitems[i + scrollPos]);
  }
  lcd.setCursor(19, (y - scrollPos));
  lcd.blink();
}

void Scrollingnumbers(int x, int y, int z, int a) {
  const int numLcdRows = 3;
  int scrollPos = 0;
  Nbitems = x;

  if (y > numLcdRows) {
    scrollPos = y - numLcdRows;
  } else {
    scrollPos = 0;
  }
  clearPrintTitle();
  for (byte i = 0; i < numLcdRows; ++i) {
    lcd.setCursor(0, i + 1);
    if (y < 4) {
      lcd.print((z - a) + (i * a) + (a));
    }
    else if (y < x) {
      lcd.print((z - a) + (i * a) + ((y - 2)*a));
    }
    else if (y == x) {
      lcd.print("back");
    }
    else {}
  }
  lcd.setCursor(19, (y - scrollPos));
  lcd.blink();
}
void clearPrintTitle() {
  lcd.noBlink();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("**ROBOT-SERRE**V.1**");
}
//-----------------DISPLAY-------------------------
//Scrollingmenu(Nbitems, Item1, item2, item3, item4, item5, Item6, item7, item8, item9, item10, currentMenuItem); //leave variable "Itemx" blank if it doesnt exist
//Scrollingnumbers(Nbitems, currentMenuItem, starting number, multiplicator);

void displayMenu(int x) {
  switch (x) {
    case 0: Scrollingmenu(6, "Capteurs", "Date/time", "Rollups", "Ventilation", "Chauffage", "Programmes", "", "", "", "", currentMenuItem); break;
    case 1: Scrollingmenu(6, "Date", "Time", "SetDOW", "Set date", "Set time", "back", "", "", "", "", currentMenuItem); break;
    case 11: Scrollingmenu(8, "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday", "back", "", "", currentMenuItem); break;
    case 12: Scrollingnumbers(11, currentMenuItem, 2016, 1);  break;
    case 121: Scrollingnumbers(13, currentMenuItem, 1 , 1);  break;
    case 1211: Scrollingnumbers(32, currentMenuItem, 1, 1); break;
    case 13: Scrollingnumbers(25, currentMenuItem, 1, 1); break;
    case 131: Scrollingnumbers(62, currentMenuItem, 0 , 1); break;
    case 1311: Scrollingnumbers(62, currentMenuItem, 0, 1); break;
    case 2: Scrollingmenu(6, "Etat", "Programme", "Set hysteresis", "Set rotation time(s)", "Set PAUSE time(s)", "back", "", "", "", "", currentMenuItem); break;
    case 21: Scrollingnumbers(6, currentMenuItem, 1, 1); break;
    case 22: Scrollingnumbers(21, currentMenuItem, 1, 1); break;
    case 23: Scrollingmenu(10, "5", "15", "20", "30", "45", "60", "75", "90", "120", "back", currentMenuItem); break;
    case 3: Scrollingmenu(3, "Etat", "Set hysteresis", "back", "", "", "", "", "", "", "", currentMenuItem); break;
    case 31: Scrollingnumbers(6, currentMenuItem, 1, 1); break;
    case 4: Scrollingmenu(5, "(F1)Etat", "(F2)Etat", "(F1)Set hysteresis", "(F2)Set hysteresis", "back", "", "", "", "", "", currentMenuItem); break;
    case 41: Scrollingnumbers(6, currentMenuItem, 1, 1); break;
    case 42: Scrollingnumbers(6, currentMenuItem, 1, 1); break;
    case 5: Scrollingmenu(10, "Programme 1", "Programme 2", "Programme 3", "Modificateurs", "Set Programme 1", "Set Programme 2", "Set Programme 3", "Set Modificateurs", "Set Ramping", "back", currentMenuItem); break;
    case 51: Scrollingmenu(3, "Set Heure", "Set Temp. cible", "back", "", "", "", "", "", "", "", currentMenuItem); break;
    case 511: Scrollingnumbers(121, currentMenuItem, -60, 1); break;
    case 512: Scrollingnumbers(51, currentMenuItem, 0 , 1); break;
    case 52: Scrollingmenu(3, "Set Heure", "Set Temp. cible", "back", "", "", "", "", "", "", "", currentMenuItem); break;
    case 521: Scrollingnumbers(25, currentMenuItem, 1, 1); break;
    case 5211: Scrollingnumbers(62, currentMenuItem, 0, 1); break;
    case 522: Scrollingnumbers(51, currentMenuItem, 0, 1); break;
    case 53: Scrollingmenu(3, "Set Heure", "Set Temp. cible", "back", "", "", "", "", "", "", "", currentMenuItem); break;
    case 531: Scrollingnumbers(121, currentMenuItem, -60, 1); break;
    case 532: Scrollingnumbers(51, currentMenuItem, 0, 1); break;
    case 54: Scrollingmenu(5, "Mod. rollups", "Mod. ventilation", "Mod. fournaise1", "Mod. fournaise2", "back", "", "", "", "", "", currentMenuItem); break;
    case 541: Scrollingnumbers(22, currentMenuItem, -10, 1); break;
    case 542: Scrollingnumbers(22, currentMenuItem, -10, 1); break;
    case 543: Scrollingnumbers(22, currentMenuItem, -10, 1); break;
    case 544: Scrollingnumbers(22, currentMenuItem, -10, 1); break;
    case 55: Scrollingnumbers(62, currentMenuItem, 0, 1); break;
  }
}
//------------------------SELECT-------------------

void switchmenu(int x) {
  delay(1000);
  Serial.print(F(",  "));
  menu = x;
  currentMenuItem = 1;
  Nbitems = 3;
  displayMenu(menu);
}

void selectMenu(int x, int y) {
  switch (x) {
    //------------------------------SelectrootMenu-------------------------------------
    //"Temperature/humidex","Date/time","Rollups","Ventilation","Chauffage"
    case 0:
      switch (y) {
        case 1:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("Sonde temp. : ")); lcd.print(sensors.getTempCByIndex(0)); lcd.print(F("C"));
          break;
        case 2: switchmenu(1); break;
        case 3: switchmenu(2); break;
        case 4: switchmenu(3); break;
        case 5: switchmenu(4); break;
        case 6: switchmenu(0); break;
      }
      break;
    //-------------------------------SelectMenu1-----------------------------------------
    //"Date","Time","Set DOW" "Set date", "Set time","back"
    case 1 :
      switch (y) {
        case 1:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(rtc.getDOWStr());
          lcd.setCursor(0, 2); lcd.print(F("Date : ")); lcd.print(rtc.getDateStr());
          break;
        case 2:
          lcd.noBlink();
          clearPrintTitle();

          t = rtc.getTime();
          sunTime[HEURE] = t.hour;
          sunTime[MINUTE] = t.min;
          sunTime[0] = t.sec;
          myLord.DST(sunTime);

          lcd.setCursor(0, 1); lcd.print(F("Time : ")); lcdPrintDigits(sunTime[HEURE]);lcd.print(":"); lcdPrintDigits(sunTime[MINUTE]);lcd.print(":");lcdPrintDigits(sunTime[MINUTE]);
          break;
        case 3: switchmenu(11); break;
        case 4: switchmenu(12); break;
        case 5: switchmenu(13); break;
        case 6: switchmenu(0); break;
      }
      break;
    //-------------------------------SelectMenu12-----------------------------------------
    //SET DAY OF THE WEEK
    case 11 :
      switch (y) {
        case 1: rtc.setDOW(MONDAY); switchmenu(1); break;
        case 2: rtc.setDOW(TUESDAY); switchmenu(1); break;
        case 3: rtc.setDOW(WEDNESDAY); switchmenu(1); break;
        case 4: rtc.setDOW(THURSDAY); switchmenu(1); break;
        case 5: rtc.setDOW(FRIDAY); switchmenu(1); break;
        case 6: rtc.setDOW(SATURDAY); switchmenu(1); break;
        case 7: rtc.setDOW(SUNDAY); switchmenu(1); break;
        case 8: switchmenu(1); break;
      }
      break;
    //-------------------------------SelectMenu12-----------------------------------------
    //SET YEAR
    case 12 :
      if (y < Nbitems) {
        yr = (2015 + y);
        switchmenu(121);
      }
      else {
        switchmenu(1);
      }
      break;
    //-------------------------------SelectMenu131-----------------------------------------
    //SET MONTH
    case 121 :
      if (y < Nbitems) {
        mt = y;
        switchmenu(1211);
      }
      else {
        switchmenu(1);
      }
      break;
    //-------------------------------SelectMenu1311-----------------------------------------
    //SET DAY
    case 1211 :
      if (y < Nbitems) {
        dy = y;
        rtc.setDate(dy, mt, yr);
        switchmenu(1);
      }
      else {
        switchmenu(1);
      }
      break;
    //-------------------------------SelectMenu14-----------------------------------------
    //SET HOUR
    case 13 :
      t = rtc.getTime();
      sunTime[HEURE] = t.hour;
      myLord.DST(sunTime);

      if ((t.hour != sunTime[HEURE]) && (y < Nbitems)) {
        hr = y-1;
        switchmenu(131);
      }
      else if ((t.hour == sunTime[HEURE]) && (y < Nbitems)) {
        hr = y;
        switchmenu(131);
      }
      else{
        switchmenu(1);
      }
      break;
    //-------------------------------SelectMenu141-----------------------------------------
    //SET MINUTES
    case 131 :
      if (y < Nbitems) {
        mn = y - 1;
        switchmenu(1311);
      }
      else {
        switchmenu(1);
      }
      break;
    //-------------------------------SelectMenu1411-----------------------------------------
    //SET SECONDS
    case 1311 :
      if (y < Nbitems) {
        sc = y - 1;
        rtc.setTime(hr, mn, sc);
        switchmenu(1);
      }
      else {
        switchmenu(1);
      }
      break;
    //-------------------------------SelectMenu2-----------------------------------------
    //"Etat", "Programme", "Set hysteresis", "Set rotation time(s)", "Set pause time(m)", "back"
    case 2 :
      switch (y) {
        case 1:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("Ouverture : ")); lcd.print(incrementCounter); lcd.print(F("%"));
          lcd.setCursor(0, 2); lcd.print(F("TP : ")); lcd.print(EEPROM.read(PAUSE)); lcd.print(F("s | TR :")); lcd.print(EEPROM.read(ROTATION)); lcd.print(F("s"));
          lcd.setCursor(0, 3); lcd.print(F("Hysteresis : ")); lcd.print(EEPROM.read(41)); lcd.print(F("C"));
          break;
        case 2:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("Programme : ")); lcd.print(program);
          lcd.setCursor(0, 2); lcd.print(F("Temp. cible : ")); lcd.print(tempCible); lcd.print(F("C"));
          lcd.setCursor(0, 3); lcd.print(F("Temp. rollup : ")); lcd.print(tempCible + EEPROM.read(35)-10); lcd.print(F("C"));
          break;
        case 3: switchmenu(21); break;
        case 4: switchmenu(22); break;
        case 5: switchmenu(23); break;
        case 6: switchmenu(0); break;
      }
      break;
    //-------------------------------SelectMenu21-----------------------------------------
    //SET HYSTERESIS
    case 21 :
      if (y < Nbitems) {
        EEPROM.update(RHYST, y);
        switchmenu(2);
      }
      else {
        switchmenu(2);
      }
      break;
    //-------------------------------SelectMenu22-----------------------------------------
    //SET ROTATION TIME
    case 22 :
      if (y < Nbitems) {
        //rotation = y;
        EEPROM.update(ROTATION, y);
        switchmenu(2);
      }
      else {
        switchmenu(2);
      }
      break;
    //-------------------------------SelectMenu23-----------------------------------------
    //"5", "15", "20", "30", "45", "60", "75", "90", "120", "back"
    case 23 :
      int pause;
      switch (y) {
        case 1: pause = 5; switchmenu(2); EEPROM.write(PAUSE, pause);break;
        case 2: pause = 15; switchmenu(2); EEPROM.write(PAUSE, pause); break;
        case 3: pause = 20; switchmenu(2); EEPROM.write(PAUSE, pause); break;
        case 4: pause = 30; switchmenu(2); EEPROM.write(PAUSE, pause); break;
        case 5: pause = 45; switchmenu(2); EEPROM.write(PAUSE, pause); break;
        case 6: pause = 60; switchmenu(2); EEPROM.write(PAUSE, pause); break;
        case 7: pause = 75; switchmenu(2); EEPROM.write(PAUSE, pause); break;
        case 8: pause = 90; switchmenu(2); EEPROM.write(PAUSE, pause); break;
        case 9: pause = 120; switchmenu(2); EEPROM.write(PAUSE, pause); break;
        case 10: switchmenu(2); break;
      }
      break;

    //-------------------------------SelectMenu3-----------------------------------------
    case 3 :
      switch (y) {
        case 1:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("FAN : "));
          if (digitalRead(FAN[0]) == OFF) {
            lcd.print(F("OFF"));
          }
          else {
            lcd.print(F("ON"));
          }
          lcd.setCursor(0, 2); lcd.print(F("Temp. cible : ")); lcd.print(tempFan[0]); lcd.print(F("C"));
          lcd.setCursor(0, 3); lcd.print(F("Hysteresis : ")); lcd.print(EEPROM.read(VHYST)); lcd.print(F("C"));
          break;
        case 2: switchmenu(31); break;
        case 3: switchmenu(0); break;
      }
      break;
    //-------------------------------SelectMenu31-----------------------------------------
    //SET HYSTERESIS
    case 31 :
      if (y < Nbitems) {
        switchmenu(3);
        EEPROM.write(1, y);
      }
      else {
        switchmenu(3);
      }
      break;
    //-------------------------------SelectMenu4-----------------------------------------
    case 4 :
      switch (y) {
        case 1:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("FOURNAISE(1) : "));
          if (digitalRead(CHAUFFAGE[0]) == OFF) {
            lcd.print(F("OFF"));
          }
          else {
            lcd.print(F("ON"));
          }
          lcd.setCursor(0, 2); lcd.print(F("Temp. cible : ")); lcd.print(tempHeater[0]); lcd.print(F("C"));
          lcd.setCursor(0, 3); lcd.print(F("Hysteresis : ")); lcd.print(EEPROM.read(HHYST)); lcd.print(F("C"));
          break;
        case 2:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("FOURNAISE(2) : "));
          if (digitalRead(CHAUFFAGE[1]) == OFF) {
            lcd.print(F("OFF"));
          }
          else {
            lcd.print(F("ON"));
          }
          lcd.setCursor(0, 2); lcd.print(F("Temp. cible : ")); lcd.print(tempHeater[1]); lcd.print(F("C"));
          lcd.setCursor(0, 3); lcd.print(F("Hysteresis : ")); lcd.print(EEPROM.read(HHYST+1)); lcd.print(F("C"));
          break;
        case 3: switchmenu(41); break;
        case 4: switchmenu(42); break;
        case 5: switchmenu(0); break;
      }
      break;
    //-------------------------------SelectMenu41-----------------------------------------
    //SET HYSTERESIS
    case 41 :
      if (y < Nbitems) {
        switchmenu(4);
        EEPROM.update(HHYST, y);
      }
      else {
        switchmenu(4);
      }
      break;
    //-------------------------------SelectMenu42-----------------------------------------
    //SET HYSTERESIS
    case 42 :
      if (y < Nbitems) {
        switchmenu(4);
        EEPROM.write(HHYST+1, y);
      }
      else {
        switchmenu(4);
      }
      break;
    //-------------------------------SelectMenu5-----------------------------------------
    //"Programme 1", "Programme 2", "Programme 3", "Modificateurs" "Set Programme 1", "Set Programme 2", "Set Programme 3", "Set Modificateurs", "back"
    case 5 :/*
      switch (y) {
        case 1:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("HD: ")); lcd.print(P[0][HEURE]); lcd.print(F(":")); lcd.print(P[0][MINUTE]); lcd.print(F(" | HF: ")); lcd.print(P[1][HEURE]); lcd.print(F(":")); lcd.print(P[1][MINUTE]);
          //lcd.setCursor(0, 2); lcd.print(F("TEMP.CIBLE : ")); lcd.print(tempCibleE[0]); lcd.print(F("C"));
          //lcd.setCursor(0,3); lcd.print(F("RAMPING : "));  lcd.print(ramping); lcd.print(F(" min"));
          break;
        case 2:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("HD: ")); lcd.print(P[1][HEURE]); lcd.print(F(":")); lcd.print(P[1][MINUTE]); lcd.print(F(" | HF: ")); lcd.print(P[2][HEURE]); lcd.print(F(":")); lcd.print(P[2][HEURE]);
          //lcd.setCursor(0, 2); lcd.print(F("TEMP.CIBLE : ")); lcd.print(tempCibleE[1]); lcd.print(F("C"));
          //lcd.setCursor(0,3); lcd.print(F("RAMPING : "));  lcd.print(RAMPING); lcd.print(F(" min"));
          break;
        case 3:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("HD: ")); lcd.print(P[2][HEURE]); lcd.print(F(":")); lcd.print(P[2][HEURE]); lcd.print(F(" | HF: ")); lcd.print(P[0][HEURE]); lcd.print(F(":")); lcd.print(P[0][MINUTE]);
          //lcd.setCursor(0, 2); lcd.print(F("TEMP.CIBLE : ")); lcd.print(tempCibleE[2]); lcd.print(F("C"));
          //lcd.setCursor(0,3); lcd.print(F("RAMPING : "));  lcd.print(RAMPING); lcd.print(F(" min"));
          break;
        case 4:
          lcd.noBlink();
          clearPrintTitle();
          lcd.setCursor(0, 1); lcd.print(F("TEMP.CIBLE : ")); lcd.print(tempCible);
          lcd.setCursor(0, 2); lcd.print(F("RMod:")); lcd.print(EEPROM.read(RMOD)-11);
          lcd.setCursor(8, 2); lcd.print(F("| HMOD[0]:")); lcd.print(EEPROM.read(HMOD)-11);
          lcd.setCursor(0, 3); lcd.print(F("VMOD[0]:")); lcd.print(EEPROM.read(VMOD)-11);
          lcd.setCursor(8, 3); lcd.print(F("| HMOD[1]:")); lcd.print(EEPROM.read(HMOD+1)-11);
          break;
        case 5: switchmenu(51); break;
        case 6: switchmenu(52); break;
        case 7: switchmenu(53); break;
        case 8: switchmenu(54); break;
        case 9: switchmenu(55); break;
        case 10: switchmenu(0); break;
      }*/
      break;
    //-------------------------------SelectMenu51-----------------------------------------
    case 51:
      switch (y) {
        case 1: switchmenu(511);break;
        case 2: switchmenu(512); break;
        case 3: switchmenu(5); break;
      }
      break;
    //-------------------------------SelectMenu511-----------------------------------------
    //SET MINUTES PAST/BEFORE SUNRISE
    case 511 :
      if (y < Nbitems) {
        EEPROM.write(SSMOD, y);
        switchmenu(51);
      }
      else {
        switchmenu(51);
      }
      break;
    //-------------------------------SelectMenu512-----------------------------------------
    //SET tempCible
    case 512 :
      if (y < Nbitems) {
        //tempCibleE[0] = y-1;
        switchmenu(51);
        //EEPROM.write(TEMP_CIBLEE, y-1);
      }
      else {
        switchmenu(51);
      }
      break;
    //-------------------------------SelectMenu52-----------------------------------------
    case 52:
      switch (y) {
        case 1: switchmenu(521);break;
        case 2: switchmenu(522); break;
        case 3: switchmenu(5); break;
      }
      break;
    //-------------------------------SelectMenu521-----------------------------------------
    //SET HOUR
    case 521 :
      if (y < Nbitems) {
        P[1][HEURE] = y;
        switchmenu(5211);
        EEPROM.write(7, P[1][HEURE]);
      }
      else {
        switchmenu(52);
      }
      break;
    //-------------------------------SelectMenu5211-----------------------------------------
    //SET MINUTES
    case 5211 :
      if (y < Nbitems) {
        P[1][MINUTE] = (y - 1);
        switchmenu(52);
        EEPROM.write(8, P[1][MINUTE]);
      }
      else {
        switchmenu(52);
      }
      break;
    //-------------------------------SelectMenu522-----------------------------------------
    //SET tempCible
    case 522 :
      if (y < Nbitems) {
        //tempCibleE[1] = y-1;
        switchmenu(52);
        //EEPROM.write(9, tempCibleE[1]);
      }
      else {
        switchmenu(52);
      }
      break;
    //-------------------------------SelectMenu53-----------------------------------------
    //"Heure", "Tempcible", "back"
    case 53:
      switch (y) {
        case 1: switchmenu(531); break;
        case 2: switchmenu(532); break;
        case 3: switchmenu(5); break;
      }
      break;
    //-------------------------------SelectMenu531-----------------------------------------
    //SET MINUTES PAST/BEFORE SUNSET
    case 531 :
      if (y < Nbitems) {
        //SSMOD[0] = y;
        //EEPROM.write(10, SSMOD[0]);
        switchmenu(53);
      }
      else {
        switchmenu(53);
      }
      break;
    //-------------------------------SelectMenu532-----------------------------------------
    //SET tempCible
    case 532 :
      if (y < Nbitems) {
        //tempCibleE[2] = y-1;
        switchmenu(53);
        //EEPROM.write(12, tempCibleE[2]);
      }
      else {
        switchmenu(53);
      }
      break;
    //-------------------------------SelectMenu54-----------------------------------------
    //
    case 54:
      switch (y) {
        case 1: switchmenu(541); break;
        case 2: switchmenu(542); break;
        case 3: switchmenu(543); break;
        case 4: switchmenu(544); break;
        case 5: switchmenu(5); break;
      }
      break;
    //-------------------------------SelectMenu541-----------------------------------------
    //SET MOD
    case 541 :
      if (y < Nbitems) {
        //tempRollup = tempCible + y -11;
        switchmenu(54);
        EEPROM.write(15, y);
      }
      else {
        switchmenu(54);
      }
      break;
    //-------------------------------SelectMenu541-----------------------------------------
    //SET MOD
    case 542 :
      if (y < Nbitems) {
        tempFan[0] = tempCible + y-11;
        switchmenu(54);
        EEPROM.write(16, y);
      }
      else {
        switchmenu(54);
      }
      break;
    //-------------------------------SelectMenu541-----------------------------------------
    //SET MOD
    case 543 :
      if (y < Nbitems) {
        tempHeater[0] = tempCible + y -11;
        switchmenu(54);
        EEPROM.write(17, y);
      }
      else {
        switchmenu(54);
      }
      break;
    //-------------------------------SelectMenu541-----------------------------------------
    //SET MOD
    case 544 :
      if (y < Nbitems) {
        tempHeater[1] = tempCible + y-11;
        switchmenu(54);
        EEPROM.write(18, y);
      }
      else {
        switchmenu(54);
      }
      break;//-------------------------------SelectMenu541-----------------------------------------
    //SET ramping interval
    case 55:/*
    if (y < Nbitems) {
        RAMPING = y-1;
        EEPROM.read(RAMPING)*60*1000 = ((unsigned long)y-1)*60*1000;
        EEPROM.write(19, RAMPING);
        switchmenu(5);
      }
      else {
        switchmenu(5);
      }*/
      break;
  }

}
