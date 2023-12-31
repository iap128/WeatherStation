#include "Credentials.h"
#include <Wire.h>
#include <WiFi.h>
#include "SparkFunBME280.h"
#include <MySQL_Generic.h>

#define MYSQL_DEBUG_PORT      Serial
#define _MYSQL_LOGLEVEL_      0


BME280 tempSensor;

uint16_t server_port = 3306;

char default_database[] = "weather";
char default_table[]    = "temperature";

MySQL_Connection conn((Client *)&client);

MySQL_Query *query_mem;


int WSPEED = D0;  //Digital I/O pin for wind speed
int WDIR = A1;  //Analog pin for wind direction
int RAIN = D1;  //Digital I/O pin for rain fall
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

//Global Variables
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
long lastSecond; //The millis counter to see when a second rolls by
byte seconds; //When it hits 60, increase the current minute
byte seconds_2m; //Keeps track of the "wind speed/dir avg" over last 2 minutes array of data
byte minutes; //Keeps track of where we are in various arrays of data
byte minutes_10m; //Keeps track of where we are in wind gust/dir over last 10 minutes array of data

long lastWindCheck = 0;
volatile long lastWindIRQ = 0;
volatile byte windClicks = 0;

byte windspdavg[120]; //120 bytes to keep track of 2 minute average

#define WIND_DIR_AVG_SIZE 120
float temperature;
float humidity;
float pressure;
int winddiravg[WIND_DIR_AVG_SIZE]; //120 ints to keep track of 2 minute average
float windgust_10m[10]; //10 floats to keep track of 10 minute max
int windgustdirection_10m[10]; //10 ints to keep track of 10 minute max
volatile float rainHour[60]; //60 floating numbers to keep track of 60 minutes of rain

//These are all the weather values that wunderground expects:
float windspdmph_avg2m = 0; // [mph 2 minute average wind speed mph]
int winddir_avg2m = 0; // [0-360 2 minute average wind direction]
float windgustmph_10m = 0; // [mph past 10 minutes wind gust mph ]
int windgustdir_10m = 0; // [0-360 past 10 minutes wind gust direction]
float rainin = 0; // [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min

// volatiles are subject to modification by IRQs
volatile unsigned long raintime, rainlast, raininterval, rain;

//Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void rainIRQ()
// Count rain gauge bucket tips as they occur
// Activated by the magnet and reed switch in the rain gauge, attached to input D2
{
  raintime = millis(); // grab current time
  raininterval = raintime - rainlast; // calculate interval between this and last event

  if (raininterval > 10) // ignore switch-bounce glitches less than 10mS after initial edge
  {
    rainHour[minutes] += 0.011; //Increase this minute's amount of rain

    rainlast = raintime; // set up for next event
  }
}

void wspeedIRQ()
// Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3
{
  if (millis() - lastWindIRQ > 10) // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes
  {
    lastWindIRQ = millis(); //Grab the current time
    windClicks++; //There is 1.492MPH for each click per second.
  }
}


void setup() {
  Serial.begin(115200);
  while (!Serial);  //Wait for user to open serial monitor

  Wire.begin(); //Join I2C bus

  if (tempSensor.begin() == false) { //Connect to BME280
    Serial.println("BME280 did not respond.");
    while(1); //Freeze
  }

  //connect to WiFi
  connectToWiFi(ssid, password);

  conn.connectNonBlocking(server, server_port, dbUser, dbPassword);

  pinMode(WSPEED, INPUT_PULLUP); // input from wind meters windspeed sensor
  pinMode(RAIN, INPUT_PULLUP); // input from wind meters rain gauge sensor

  seconds = 0;
  lastSecond = millis();

  // attach external interrupt pins to IRQ functions
  attachInterrupt(RAIN, rainIRQ, FALLING);
  attachInterrupt(WSPEED, wspeedIRQ, FALLING);

  // turn on interrupts
  interrupts();
}

void loop() {
  //Keep track of which minute it is
  if (millis() - lastSecond >= 1000)
  {
    lastSecond += 1000;

    //Take a speed and direction reading every second for 2 minute average
    if (++seconds_2m > 119) seconds_2m = 0;

    //Calc the wind speed and direction every second for 120 second to get 2 minute average
    float currentSpeed = get_wind_speed();
    int currentDirection = get_wind_direction();
    windspdavg[seconds_2m] = (int)currentSpeed;
    winddiravg[seconds_2m] = currentDirection;

    //Check to see if this is a gust for the minute
    if (currentSpeed > windgust_10m[minutes_10m])
    {
      windgust_10m[minutes_10m] = currentSpeed;
      windgustdirection_10m[minutes_10m] = currentDirection;
    }

    if (++seconds > 59)
    {
      seconds = 0;

      if (++minutes > 59) minutes = 0;
      if (++minutes_10m > 9) minutes_10m = 0;

      rainHour[minutes] = 0; //Zero out this minute's rainfall amount
      windgust_10m[minutes_10m] = 0; //Zero out this minute's gust
    }

    //Report all readings every second
    printWeather(seconds_2m);
  }

  delay(100);

  /*if (conn.connectNonBlocking(server, server_port, dbUser, dbPassword) != RESULT_FAIL)
  {
    delay(500);
    runInsert();
    conn.close();                     // close the connection
  } 
  else 
  {
    MYSQL_DISPLAY("\nConnect failed. Trying again on next iteration.");
  }

  delay(1000);*/
}

void connectToWiFi(const char * ssid, const char * pwd)
{
  int ledState = 0;

  printLine();
  Serial.println("Connecting to WiFi network: " + String(ssid));

  WiFi.begin(ssid, pwd);

  while (WiFi.status() != WL_CONNECTED) 
  {
    ledState = (ledState + 1) % 2; // Flip ledState
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void printLine()
{
  Serial.println();
  for (int i=0; i<30; i++)
    Serial.print("-");
  Serial.println();
}

//Calculates each of the variables that wunderground is expecting
void calcWeather()
{
  temperature = tempSensor.readTempF();
  humidity = tempSensor.readFloatHumidity();
  pressure = tempSensor.readFloatPressure()/33.864; //convert millibars to inches mercury


  //Calc windspdmph_avg2m
  float temp = 0;
  for (int i = 0 ; i < 120 ; i++)
    temp += windspdavg[i];
  temp /= 120.0;
  windspdmph_avg2m = temp;

  long sum = winddiravg[0];
  int D = winddiravg[0];
  for (int i = 1 ; i < WIND_DIR_AVG_SIZE ; i++)
  {
    int delta = winddiravg[i] - D;

    if (delta < -180)
      D += delta + 360;
    else if (delta > 180)
      D += delta - 360;
    else
      D += delta;

    sum += D;
  }
  winddir_avg2m = sum / WIND_DIR_AVG_SIZE;
  if (winddir_avg2m >= 360) winddir_avg2m -= 360;
  if (winddir_avg2m < 0) winddir_avg2m += 360;

  //Calc windgustmph_10m
  //Calc windgustdir_10m
  //Find the largest windgust in the last 10 minutes
  windgustmph_10m = 0;
  windgustdir_10m = 0;
  //Step through the 10 minutes
  for (int i = 0; i < 10 ; i++)
  {
    if (windgust_10m[i] > windgustmph_10m)
    {
      windgustmph_10m = windgust_10m[i];
      windgustdir_10m = windgustdirection_10m[i];
    }
  }

  //Total rainfall for the day is calculated within the interrupt
  //Calculate amount of rainfall for the last 60 minutes
  rainin = 0;
  for (int i = 0 ; i < 60 ; i++)
    rainin += rainHour[i];
}

//Returns the instataneous wind speed
float get_wind_speed()
{
  float deltaTime = millis() - lastWindCheck; //750ms

  deltaTime /= 1000.0; //Covert to seconds

  float windSpeed = (float)windClicks / deltaTime; //3 / 0.750s = 4

  windClicks = 0; //Reset and start watching for new wind
  lastWindCheck = millis();

  windSpeed *= 1.492; //4 * 1.492 = 5.968MPH

  return (windSpeed);
}

//Read the wind direction sensor, return heading in degrees
int get_wind_direction()
{
  unsigned int adc;

  adc = analogRead(WDIR); // get the current reading from the sensor

  // The following table is ADC readings for the wind direction sensor output, sorted from low to high.
  // Each threshold is the midpoint between adjacent headings. The output is degrees for that ADC reading.
  // Note that these are not in compass degree order! See Weather Meters datasheet for more information.

  if (adc < 380) return (113);
  if (adc < 393) return (68);
  if (adc < 414) return (90);
  if (adc < 456) return (158);
  if (adc < 508) return (135);
  if (adc < 551) return (203);
  if (adc < 615) return (180);
  if (adc < 680) return (23);
  if (adc < 746) return (45);
  if (adc < 801) return (248);
  if (adc < 833) return (225);
  if (adc < 878) return (338);
  if (adc < 913) return (0);
  if (adc < 940) return (293);
  if (adc < 967) return (315);
  if (adc < 990) return (270);
  return (-1); // error, disconnected?
}


// SQL queries
String insertTemp = "INSERT INTO weather.temperature (temp) VALUES";
String insertHumidity = "INSERT INTO weather.humidity (humidity) VALUES";
String insertPressure = "INSERT INTO weather.pressure (pressure) VALUES";
String insertWindSpeed = "INSERT INTO weather.windSpeed (speed) VALUES";
String insertWindDirection = "INSERT INTO weather.windDirection (direction) VALUES";
String insertGustSpeed = "INSERT INTO weather.gustSpeed (speed) VALUES";
String insertGustDirection = "INSERT INTO weather.gustDirection (direction) VALUES";
String insertRain = "INSERT INTO weather.rain (quantity) VALUES";



//Prints the various variables directly to the port
//I don't like the way this function is written but Arduino doesn't support floats under sprintf
void printWeather(const byte timeInterval)
{
  calcWeather(); //Go calc all the various sensors

  Serial.println();
  Serial.print("Temperature: ");
  Serial.println(temperature, 2);
  Serial.print("Humidity: ");
  Serial.println(humidity, 0);
  Serial.print("Pressure: ");
  Serial.println(pressure, 0);
  Serial.println();
  Serial.print("Wind Speed: ");
  Serial.println(windspdmph_avg2m, 1);
  Serial.print("Wind Direction: ");
  Serial.println(winddir_avg2m);
  Serial.print("Gust Speed: ");
  Serial.println(windgustmph_10m, 1);
  Serial.print("Gust Direction: ");
  Serial.println(windgustdir_10m);
  Serial.print("Rain Quantity: ");
  Serial.println(rainin, 2);
  Serial.println("------------");

  //every 2 minutes, insert the values into the SQL tables
  if (timeInterval == 119) {
    //connect to the database
    if (conn.connectNonBlocking(server, server_port, dbUser, dbPassword) != RESULT_FAIL) {
      delay(500);
      
      runInsert(insertTemp, String(temperature));
      runInsert(insertHumidity, String(humidity));
      runInsert(insertPressure, String(pressure));
      runInsert(insertWindSpeed, String(windspdmph_avg2m));
      runInsert(insertWindDirection, String(winddir_avg2m));
      runInsert(insertGustSpeed, String(windgustmph_10m));
      runInsert(insertGustDirection, String(windgustdir_10m));
      runInsert(insertRain, String(rainin));

      //close the connection after all inserts are done
      conn.close(); 
    }
  }
}


void runInsert(const String query, const String value)
{
  String fullQuery = query + " (" + value + ");";
  // Initiate the query class instance
  MySQL_Query query_mem = MySQL_Query(&conn);

  if (conn.connected())
  {
    MYSQL_DISPLAY(fullQuery);
    
    // Execute the query
    // KH, check if valid before fetching
    if ( !query_mem.execute(fullQuery.c_str()) )
    {
      MYSQL_DISPLAY("Insert error");
    }
    else
    {
      MYSQL_DISPLAY("Data Inserted.");
    }
  }
  else
  {
    MYSQL_DISPLAY("Disconnected from Server. Can't insert.");
  }
}