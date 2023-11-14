#include "twilio.hpp"
#include <WifiUDP.h>
#include <NTPClient.h>
#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <ArduinoJson.h>
#include <Arduino_JSON.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdlib.h>


Twilio *twilio;
WiFiUDP ntpUDP;


static const char *ssid = "MOVISTAR_FIBRA_8830";
static const char *password = "M9UY4FT3mtr77hormjmm";

// Definir propiedades NTP
#define NTP_OFFSET   60 * 60                                                                                               // En segundos
#define NTP_INTERVAL 60 * 1000                                                                                             // En milisegundos
#define NTP_ADDRESS  "co.pool.ntp.org"   


NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -360};  // Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -360};   // Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);

time_t local, utc;

const char * days[] = {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"} ;                        // Configurar Fecha y hora
const char * months[] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"} ;            // Configurar Fecha y hora

String horaLocal;
String diaLocal;
String fechaLocal;
int contador = 0;
bool act = false;
bool alarma = false;

//Pines de control

const int RCWL = 2;
const int RELE = 4;
const int HCSR = 34;

JSONVar horario;
JSONVar activo;
JSONVar encender;
JSONVar account_sid;
JSONVar auth_token;
JSONVar from_number;
JSONVar to_number;
JSONVar message;

HTTPClient http;
HTTPClient https;

String api ="http://64fe27f6596493f7af7ef4f3.mockapi.io";
String apiFirebase = "https://alerttheft-35367-default-rtdb.firebaseio.com/activation.json";



void setup() {
  Serial.begin(115200);
  pinMode(RCWL, INPUT);
  pinMode(HCSR, INPUT);
  pinMode(RELE, OUTPUT);
  digitalWrite(RELE, LOW);

  Serial.print("Connecting to WiFi network ;");
  Serial.print(ssid);
  Serial.println("'...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting...");
    delay(500);
  }
  Serial.println("Connected!");

}

void loop() {
  WiFiClientSecure client;
  timeClient.update();
  unsigned long utc =  timeClient.getEpochTime();
  local = usET.toLocal(utc);
  printTime(local);
  getApi(http, api + "/Horario/1");
  delay(4000);
}



void getApi(HTTPClient &http, String api) {
  int sensorValueRCWL = digitalRead(RCWL);
  int sensorValueHCSR = digitalRead(HCSR);
  http.begin(api);
  int httpCode = http.GET();
  if (httpCode > 0) {
    // Aquí puedes procesar la respuesta del servidor
    String payload = http.getString();
    JSONVar myObject = JSON.parse(payload.c_str());
    horario = myObject["schedules"];
    activo  = myObject["activo"];
    encender  = myObject["tocar"];
    account_sid = myObject["account_sid"];
    auth_token  = myObject["auth_token"];
    from_number = myObject["from_number"];
    to_number = myObject["to_number"];
    message = myObject["message"];
    int tam =horario.length(); 
    JSONVar horasI[tam];
    JSONVar horasF[tam];
    Serial.println(payload);
    if(!activo){
    for (int i = 0; i < tam; i++) {
        String hI = horario[i]["start_time"];
        String hF = horario[i]["end_time"];
        
        int horas = hF.substring(0, 2).toInt();
        int minutos = hF.substring(3).toInt();
        // Restar un minuto
        if (minutos > 0) {
          minutos--;
        } else {
          if (horas > 0) {
            horas--;
            minutos = 59;
          } else {
            horas = 23; // Si es 00:00, resta un minuto y obtén 23:59
            minutos = 59;
          }
        }
        // Formatear la nueva cadena de tiempo
          String nueva_hF = (horas < 10 ? "0" : "") + String(horas, DEC) + ":" + (minutos < 10 ? "0" : "") + String(minutos, DEC);
        // String nueva_hF = String(horas, DEC) + ":" + String(minutos, DEC);
        Serial.println("Cadena original: " + hF);
        Serial.println("Cadena con un minuto menos: " + nueva_hF);
        if (isTimeInRange(hI, nueva_hF, horaLocal)) {
            act = true;
            Serial.println("Activado");
            break;
        } else {
            act = false;
            contador = 0;
            Serial.println("Desactivado");
            break;
        }
    }
      if(act){
        Serial.println("Sistema a alarmas activado");
        if (sensorValueRCWL == HIGH || sensorValueHCSR == HIGH) {
          alarma=true;
          Serial.println("Movimiento detectado ");
        } else {
          Serial.println("Sin movimiento");
        }
        if(alarma){
          contador=contador+1;
          digitalWrite(RELE, HIGH);
          if(contador==1){
            postActivation(https, apiFirebase, fechaLocal, horaLocal);
            String response;
            twilio = new Twilio(account_sid, auth_token);
            delay(1000);
            bool success = twilio->send_message(to_number, from_number, message, response);
            if (success) {
              Serial.println("Sent message successfully!");
            } else {
              Serial.println(response);
            }
          }
        }else{
          contador=0;
          digitalWrite(RELE, LOW);
        }
      }else{
        Serial.println("Sistema a alarmas desactivado");
        alarma=false;
        contador=0;
        digitalWrite(RELE, LOW);
      }
    }else{
      alarma=false;
      act=false;
      digitalWrite(RELE, LOW);
      contador=0;
    }
  }else{
    horario = horario;
    activo = activo;
    encender = encender;
    account_sid = account_sid;
    auth_token = auth_token;
    from_number = from_number;
    to_number = to_number;
    message = message;
  }

  // Terminar la conexión HTTP
  http.end();
}

void postActivation(HTTPClient &http, String apif, String fecha, String hora){
  // WiFiClientSecure client;
  if (http.begin(apif)){
    http.addHeader("Content-Type", "application/json");
    String dat = "{\r\n    \"hora\": \"" + hora + "\",\r\n    \"fecha\": \"" + fecha + "\"\r\n}";
    const char *data= dat.c_str();
    Serial.print("[http] GET...\n");
    int httpCode = http.POST(data);
    if(httpCode > 0){
      Serial.printf("[http] GET... code: %d\n", httpCode);
      String payload = http.getString();
      Serial.println(payload);
      }else{
        Serial.printf("[http] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
    http.end();
  }
  delay(2000);
  
}

// Función para verificar si una hora está dentro de un rango de tiempo
bool isTimeInRange(String startTime, String endTime, String targetTime) {
    int startMinutes = startTime.substring(0, 2).toInt() * 60 + startTime.substring(3).toInt();
    int endMinutes = endTime.substring(0, 2).toInt() * 60 + endTime.substring(3).toInt();
    int targetMinutes = targetTime.substring(0, 2).toInt() * 60 + targetTime.substring(3).toInt();

    if (startMinutes <= endMinutes) {
        return (startMinutes <= targetMinutes && targetMinutes <= endMinutes);
    } else {
        return (targetMinutes >= startMinutes || targetMinutes <= endMinutes);
    }
}

void printTime(time_t t)                                                                                              // Funcion para mandar hora por puerto serie    
{
  Serial.print("Hora local: ");
  Serial.println(convertirTimeATextoHora(t));
  Serial.println("Dia: ");
  Serial.println(obtenerDia(t));
  fechaLocal = convertirTimeATextoFecha(t);
  Serial.println(fechaLocal);
}

String convertirTimeATextoFecha(time_t t)                                                                               // Funcion para formatear en texto la fecha  
{
  String date = "";
  date += days[weekday(t)-1];
  date += ", ";
  date += day(t);
  date += " ";
  date += months[month(t)-1];
  date += ", ";
  date += year(t);
  return date;
}
String obtenerDia(time_t t){
  String date = "";
  date += days[weekday(t)-1];
  diaLocal= date;
  return date;
}

String convertirTimeATextoFechaSinSemana(time_t t)                                                                    // Funcion para formatear en texto la fecha sin dia de la semana
{
  String date = "";
  date += months[month(t)-1];
  date += "   ";
  date += year(t);
  return date;
}

String convertirTimeATextoHora(time_t t)                                                                              // Funcion para formatear en texto la hora                                                                          
{
  String hora ="";                                                                                                    // Funcion para formatear en texto la hora  
  if(hour(t) < 10)
  hora += "0";
  hora += hour(t);
  hora += ":";
  if(minute(t) < 10)                                                                                                  // Agregar un cero si el minuto es menor de 10
    hora += "0";
  hora += minute(t);
  horaLocal= hora;
  return hora;
}
