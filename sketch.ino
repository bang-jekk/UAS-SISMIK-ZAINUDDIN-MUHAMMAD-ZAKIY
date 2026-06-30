#define BLYNK_TEMPLATE_ID "TMPL6Ac6hz3di"
#define BLYNK_TEMPLATE_NAME "Absensi RFID"
#define BLYNK_AUTH_TOKEN "5MEXMtZxw1hqh2SYtwc16dnneGG9ukFL"

#include <WiFi.h>
#include <WiFiClientSecure.h> // Ditambahkan untuk TLS/SSL
#include <PubSubClient.h>     // Ditambahkan untuk MQTT
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <MFRC522.h>

char ssid[] = "Wokwi-GUEST";
char pass[] = "";

// Konfigurasi EMQX MQTT
const char* mqtt_server   = "f940655c.ala.us-east-1.emqxsl.com";
const int mqtt_port       = 8883; // Port TLS/SSL
const char* mqtt_user     = "zainuddin";
const char* mqtt_pass     = "zainuddin";
const char* mqtt_topic    = "absen/rfid";

#define SS_PIN 5
#define RST_PIN 21
#define LED_HIJAU 26
#define LED_MERAH 27
#define BUZZER 25

MFRC522 rfid(SS_PIN, RST_PIN);
BlynkTimer timer;

WiFiClientSecure espClient; // Menggunakan secure client untuk TLS
PubSubClient mqttClient(espClient);

//==============================
// DATA MAHASISWA
//==============================
struct Mahasiswa {
  String uid;
  String nama;
};

Mahasiswa daftarMahasiswa[] = {
  {"01 02 03 04", "Zainuddin"},
  {"11 22 33 44", "lita Gemoy"},
  {"AA BB CC DD", "Hilwa"},
  {"55 66 77 88", "Windah"}
};

const int jumlahMahasiswa = sizeof(daftarMahasiswa) / sizeof(daftarMahasiswa[0]);

String uidTerakhir = "";
unsigned long waktuScan = 0;
const unsigned long jedaScan = 5000;

//==============================
String uidToString(MFRC522::Uid *uid){
  String hasil="";
  for(byte i=0; i<uid->size; i++){
    if(uid->uidByte[i]<0x10) hasil+="0";
    hasil+=String(uid->uidByte[i],HEX);
    if(i<uid->size-1) hasil+=" ";
  }
  hasil.toUpperCase();
  return hasil;
}

//==============================
String cariNama(String uid){
  for(int i=0; i<jumlahMahasiswa; i++){
    if(uid==daftarMahasiswa[i].uid){
      return daftarMahasiswa[i].nama;
    }
  }
  return "";
}

//==============================
void buzzerOK(){
  tone(BUZZER,1500);
  delay(150);
  noTone(BUZZER);
}

//==============================
void buzzerGagal(){
  tone(BUZZER,600);
  delay(700);
  noTone(BUZZER);
}

//==============================
void lampuHijau(){
  digitalWrite(LED_HIJAU,HIGH);
  digitalWrite(LED_MERAH,LOW);
}

//==============================
void lampuMerah(){
  digitalWrite(LED_HIJAU,LOW);
  digitalWrite(LED_MERAH,HIGH);
}

//==============================
void resetLampu(){
  digitalWrite(LED_HIJAU,LOW);
  digitalWrite(LED_MERAH,LOW);
}

//==================================================
// FUNGSI KONEKSI MQTT
//==================================================
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Menghubungkan ke EMQX MQTT...");
    // Membuat Client ID acak berbasis Mac Address / Millis
    String clientId = "ESP32Client-" + String(random(0, 0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("Terhubung ke EMQX!");
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Mencoba lagi dalam 5 detik...");
      delay(5000);
    }
  }
}

//==============================
void setup(){
  Serial.begin(115200);
  pinMode(LED_HIJAU,OUTPUT);
  pinMode(LED_MERAH,OUTPUT);
  pinMode(BUZZER,OUTPUT);

  resetLampu();
  SPI.begin();
  rfid.PCD_Init();

  Serial.println("=======================");
  Serial.println("ABSENSI RFID ESP32");
  Serial.println("=======================");

  WiFi.begin(ssid,pass);
  Serial.print("Menghubungkan WiFi");
  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  // Setup TLS Client (Mengabaikan validasi sertifikat root agar simpel namun tetap terenkripsi)
  espClient.setInsecure(); 
  mqttClient.setServer(mqtt_server, mqtt_port);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  Serial.println("Blynk Connected");
}

//==================================================
// FUNGSI KIRIM DATA KE BLYNK
//==================================================
void kirimKeBlynk(String nama, String uid, String status) {
  Blynk.virtualWrite(V0, nama);
  Blynk.virtualWrite(V1, uid);
  Blynk.virtualWrite(V2, status);
  String waktu = String(millis() / 1000) + " detik";
  Blynk.virtualWrite(V3, waktu);
}

//==================================================
// FUNGSI KIRIM DATA KE EMQX MQTT
//==================================================
void kirimKeMQTT(String nama, String uid, String status) {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  
  // Format data ke JSON string
  String payload = "{\"nama\":\"" + nama + "\",\"uid\":\"" + uid + "\",\"status\":\"" + status + "\"}";
  
  if(mqttClient.publish(mqtt_topic, payload.c_str())) {
    Serial.println("Data berhasil dipublikasikan ke EMQX!");
  } else {
    Serial.println("Gagal mempublikasikan data ke EMQX.");
  }
}

//==================================================
// PROSES ABSENSI
//==================================================
void prosesAbsensi() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uid = uidToString(&rfid.uid);

  Serial.println();
  Serial.println("==============================");
  Serial.print("UID : ");
  Serial.println(uid);
  
  // Anti Double Scan
  if (uid == uidTerakhir && millis() - waktuScan < jedaScan) {
    Serial.println("Kartu sudah discan!");
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  uidTerakhir = uid;
  waktuScan = millis();
  
  String nama = cariNama(uid);
  if (nama != "") {
    Serial.println("Status : DITERIMA");
    Serial.print("Nama   : ");
    Serial.println(nama);

    lampuHijau();
    buzzerOK();
    
    // Kirim data ke kedua platform
    kirimKeBlynk(nama, uid, "HADIR");
    kirimKeMQTT(nama, uid, "HADIR");
  } else {
    Serial.println("Status : DITOLAK");

    lampuMerah();
    buzzerGagal();
    
    // Kirim data ke kedua platform
    kirimKeBlynk("Tidak Dikenal", uid, "DITOLAK");
    kirimKeMQTT("Tidak Dikenal", uid, "DITOLAK");
  }
  
  delay(1500);
  resetLampu();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

//==================================================
// LOOP UTAMA
//==================================================
void loop() {
  Blynk.run();
  timer.run();

  // Memastikan MQTT tetap terhubung
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Jika WiFi terputus
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Terputus...");
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nWiFi Tersambung Kembali");
  }

  prosesAbsensi();
}