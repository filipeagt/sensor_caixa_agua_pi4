#include <ESP8266WiFi.h> 
#include <PubSubClient.h>

//WiFi
const char* SSID = "T350";                // SSID / nome da rede WiFi que deseja se conectar
const char* PASSWORD = "vasquinhos";   // Senha da rede WiFi que deseja se conectar
WiFiClient wifiClient;                        
 
//MQTT Server
const char* BROKER_MQTT = "test.mosquitto.org"; //URL do broker MQTT que se deseja utilizar
int BROKER_PORT = 1883;                      // Porta do Broker MQTT

#define ID_MQTT  "LhczCQYoOBMMFQU5ABUbFTI" //Informe um ID unico e seu. Caso sejam usados IDs repetidos a ultima conexão irá sobrepor a anterior. 
#define TOPIC "pi/reservatorio/nivel"    //channels/<channelID>/publish/fields/field<fieldnumber> 
//#define USUARIO_MQTT "LhczCQYoOBMMFQU5ABUbFTI" //Usúario do broker
//#define SENHA_MQTT "XuVt0v7/G3VDLhD+JRSV3UhS" //Senha do broker
PubSubClient MQTT(wifiClient);        // Instancia o Cliente MQTT passando o objeto espClient

//Varáveis para medir e armazenar o nível de água
char nivel[4];
int level = 0;
int distFundo = 100; //Distancia em cm em que o sensor está instalado do fundo da caixa

//Conexão do sensor
#define echoPin 3 //Conectar por um resistor de 1k para limitar a corrente no boot, ao ligar o pino3 inicia em high
#define trigPin 1 //0,1 e 2 se conecatdos a LOW na inicialização da falha no boot, então o esp não iniciliza se ligados no echo

long duration; //tempo que q onda trafega
int distance; //distância medida

unsigned long tempoAnterior = 0;//Váriavel que armazena o tempo em ms desde que o programa está rodando
const long intervalo = 30000;//30 segundos

//Declaração das Funções
void mantemConexoes();  //Garante que as conexoes com WiFi e MQTT Broker se mantenham ativas
void conectaWiFi();     //Faz conexão com WiFi
void conectaMQTT();     //Faz conexão com Broker MQTT
void medeDistancia();
void enviaNivel();

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  //Serial.begin(115200);

  conectaWiFi();
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
}

void loop() {
  unsigned long tempoAtual = millis();

  mantemConexoes();
  if (tempoAtual - tempoAnterior >= intervalo) { //Envia  o dado de acordo com o intervalo definido
    tempoAnterior = tempoAtual;
    enviaNivel();
  }
  MQTT.loop();  
}

void mantemConexoes() {
    if (!MQTT.connected()) {
       conectaMQTT(); 
    }
    
    conectaWiFi(); //se não há conexão com o WiFI, a conexão é refeita
}

void conectaWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
     return;
  }
  /*      
  Serial.print("Conectando-se na rede: ");
  Serial.print(SSID);
  Serial.println("  Aguarde!");
  */
  WiFi.mode(WIFI_STA); //Add pelo exemplo do NTPClient, configura o esp como estação
  WiFi.begin(SSID, PASSWORD); // Conecta na rede WI-FI  
  while (WiFi.status() != WL_CONNECTED) {
      delay(100);
      //Serial.print(".");
  }
  /*
  Serial.println();
  Serial.print("Conectado com sucesso, na rede: ");
  Serial.print(SSID);  
  Serial.print("  IP obtido: ");
  Serial.println(WiFi.localIP());
  */ 
}

void conectaMQTT() { 
    while (!MQTT.connected()) {
        //Serial.print("Conectando ao Broker MQTT: ");
        //Serial.println(BROKER_MQTT);
        if (MQTT.connect(ID_MQTT/*, USUARIO_MQTT, SENHA_MQTT*/)) {
            //Serial.println("Conectado ao Broker com sucesso!");
        } 
        else {
            //Serial.println("Não foi possivel se conectar ao broker.");
            //Serial.println("Nova tentatica de conexao em 10s");
            delay(10000);
        }
    }
}

void medeDistancia() {

  digitalWrite(trigPin, LOW);//Limpa a condição do trigPin
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);//Seta em nível alto por 10 microsegundos
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH); //Retorna o tempo que a onda trafegou
  distance = duration * 0.034 / 2; //* Velocidade do som dividida por 2 (ida e volta)
}

void enviaNivel() {
  medeDistancia();
  level = distFundo - distance; //Sensor posicionado a 1 metro (100cm) do fundo do reservatório
  //Serial.println("nível: "+ level);
  itoa(level, nivel, 10); //Converte int para char* para envio via MQTT
  MQTT.publish(TOPIC, nivel); //envia apenas o nível atual
}
