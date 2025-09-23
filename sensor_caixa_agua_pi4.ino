#include <ESP8266WiFi.h>
#include <PubSubClient.h>

//WiFi
const char* SSID = "Rede";            // SSID / nome da rede WiFi que deseja se conectar
const char* PASSWORD = "qwe@242526";  // Senha da rede WiFi que deseja se conectar
WiFiClient wifiClient;

//MQTT Server
const char* BROKER_MQTT = "test.mosquitto.org";  //URL do broker MQTT que se deseja utilizar
int BROKER_PORT = 1883;                          // Porta do Broker MQTT

#define ID_MQTT "LhczCQYoOBMMFQU5ABUbFTI"      //Informe um ID unico e seu. Caso sejam usados IDs repetidos a ultima conexão irá sobrepor a anterior.
#define TOPIC_VOLUME "pi/reservatorio/volume"  //channels/<channelID>/publish/fields/field<fieldnumber>
#define TOPIC_VAZAO "pi/reservatorio/vazao"
#define TOPIC_BOMBA "pi/reservatorio/bomba"
//#define USUARIO_MQTT "LhczCQYoOBMMFQU5ABUbFTI" //Usúario do broker
//#define SENHA_MQTT "XuVt0v7/G3VDLhD+JRSV3UhS" //Senha do broker
PubSubClient MQTT(wifiClient);  // Instancia o Cliente MQTT passando o objeto espClient

//Varáveis para medir e armazenar o nível de água
char volumeChar[5];
char vazaoChar[5];
int level = 0;
int distFundo = 100;  //Distancia em cm em que o sensor está instalado do fundo da caixa

//Conexões sensores e bomba
#define echoPin 3  //Conectar por um resistor de 1k para limitar a corrente no boot, ao ligar o pino3 inicia em high
#define trigPin 1  //0,1 e 2 se conecatdos a LOW na inicialização da falha no boot, então o esp não iniciliza se ligados no echo
#define pumpPin 0  //Saída que controla o relê da bomba
#define flowPin 2 //definicao do pino do sensor de vazão e de interrupcao

int pumpOn = 0;        //Variável para armazenar o estado da bomba
int volumeTanque = 0;  // Volume atual (L)
// Volumes mínimo e máximo em litros
const int MIN = 250;
const int MAX = 950;

long duration;  //tempo que q onda trafega
int distance;   //distância medida

unsigned long tempoAnterior = 0;  //Váriavel que armazena o tempo em ms desde que o programa está rodando
const long intervalo = 10000;     //10 segundos - intervalo de tempo

//definicao da variavel de contagem de pulsos para o sensor de vazão
volatile unsigned int contador = 0;

//definicao da variavel de fluxo
float fluxo = 0;

//Declaração das Funções
void mantemConexoes();  //Garante que as conexoes com WiFi e MQTT Broker se mantenham ativas
void conectaWiFi();     //Faz conexão com WiFi
void conectaMQTT();     //Faz conexão com Broker MQTT
void medeDistancia();
void enviaDados();
void calculaVolume();
void controlaBomba();
void recebePacote(char* topic, byte* payload, unsigned int length);
ICACHE_RAM_ATTR void contador_pulso(); //para rodar na RAM

void setup() {
  // Configuração dos pinos de conexão com os sensores e bomba
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(pumpPin, OUTPUT);
  pinMode(flowPin, INPUT_PULLUP);

  //Serial.begin(115200);

  conectaWiFi();
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
  MQTT.setCallback(recebePacote);

  //Habilita a interrupção do pino do sensor de fluxo
  attachInterrupt(digitalPinToInterrupt(flowPin), contador_pulso, RISING);
}

void loop() {
  unsigned long tempoAtual = millis();

  mantemConexoes();
  // Faz as leituras dos sensores e envia os dados de acordo com o intervalo definido
  if (tempoAtual - tempoAnterior >= intervalo) {
    tempoAnterior = tempoAtual;

    //desabilita a interrupcao para realizar a conversao do valor de pulsos
    noInterrupts();
    unsigned int contagem = contador;
    contador = 0; // Reinicia o contador de pulsos
    interrupts();

    //conversao do valor de pulsos para L/h
    fluxo = contagem * 2.25;     // pulsos * 2,25 ml
    fluxo /= (intervalo / 1000.0); // volume / intervalo (ml/s)
    fluxo *=  (3600.0);            // converte de ml/s para ml/h
    fluxo /= 1000.0;               // converte ml/h para L/h

    //exibicao do valor de fluxo
    /*Serial.print("Fluxo de: ");
    Serial.print(fluxo);
    Serial.println(" L/h");*/

    calculaVolume();
    controlaBomba();
    enviaDados();

  }

  MQTT.loop();
  // para dar tempo ao Wi-Fi processar eventos
  yield(); // ou delay(0);
}

void mantemConexoes() {
  if (!MQTT.connected()) {
    conectaMQTT();
  }

  conectaWiFi();  //se não há conexão com o WiFI, a conexão é refeita
}

void conectaWiFi() {
  int i = 0;  //contador iteração
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  /*      
  Serial.print("Conectando-se na rede: ");
  Serial.print(SSID);
  Serial.println("  Aguarde!");
  */
  WiFi.mode(WIFI_STA);         //Add pelo exemplo do NTPClient, configura o esp como estação
  WiFi.begin(SSID, PASSWORD);  // Conecta na rede WI-FI
  while (WiFi.status() != WL_CONNECTED) {
    i++;
    delay(100);
    //Serial.print(".");
    if (i >= 100) {  // passaram 10 segundos
      i = 0;
      calculaVolume();
      controlaBomba();
    }
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
    if (MQTT.connect(ID_MQTT /*, USUARIO_MQTT, SENHA_MQTT*/)) {
      //Serial.println("Conectado ao Broker com sucesso!");
      MQTT.subscribe(TOPIC_BOMBA);
    } else {
      //Serial.println("Não foi possivel se conectar ao broker.");
      //Serial.println("Nova tentatica de conexao em 10s");
      delay(10000);
      calculaVolume();
      controlaBomba();
    }
  }
}

void medeDistancia() {

  digitalWrite(trigPin, LOW);  //Limpa a condição do trigPin
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);  //Seta em nível alto por 10 microsegundos
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH);  //Retorna o tempo que a onda trafegou
  distance = duration * 0.034 / 2;    //* Velocidade do som dividida por 2 (ida e volta)
}

void enviaDados() {
  //Serial.println("nível: "+ level);
  itoa(volumeTanque, volumeChar, 10);      //Converte int para char* para envio via MQTT
  MQTT.publish(TOPIC_VOLUME, volumeChar);  //envia o volume atual
  itoa(round(fluxo), vazaoChar, 10);
  MQTT.publish(TOPIC_VAZAO, vazaoChar);
}

//funcao chamada pela interrupcao para contagem de pulsos
ICACHE_RAM_ATTR void contador_pulso() {
  // Incrementa o contador a cada pulso
  contador++;
}

void calculaVolume() {
  medeDistancia();
  level = distFundo - distance;  //Sensor posicionado a 1 metro (100cm) do fundo do reservatório
  /*  Dimensões da caixa de água
  Raio superior = 0,72m
  Raio inferior = 0,57m
  Altura = 0,84m
  tangente do angulo de inclinação da parede = 0,18 
  */
  //Volume do tronco de cone V = 1/3*pi*h*(R²+r²+Rr)
  float h = level / 100.0;  //nível da água em metros
  float r = 0.57;
  float R = r + 0.18 * h;
  volumeTanque = round((0.333 * 3.14 * h * (R * R + r * r + R * r)) * 1000);  // * 1000 Converte de m³ para litros
}

void controlaBomba() {  // Controle automático da bomba
  pumpOn = digitalRead(pumpPin);
  if (volumeTanque <= MIN && !pumpOn) {
    digitalWrite(pumpPin, HIGH);
    if (MQTT.connected()) {
      MQTT.publish(TOPIC_BOMBA, "on_OK");
    }
  } else if (volumeTanque >= MAX && pumpOn) {
    digitalWrite(pumpPin, LOW);
    if (MQTT.connected()) {
      MQTT.publish(TOPIC_BOMBA, "off_OK");
    }
  }
}

void recebePacote(char* topic, byte* payload, unsigned int length) {
  String msg;
  pumpOn = digitalRead(pumpPin);
  //obtem a string do payload recebido
  for (int i = 0; i < length; i++) {
    char c = (char)payload[i];
    msg += c;
  }
  //Serial.println(msg);
  // Controla a bomba evitando erros do usuário
  if (msg == "on" && !pumpOn) {
    if (volumeTanque < MAX) {
      digitalWrite(pumpPin, HIGH);
      MQTT.publish(TOPIC_BOMBA, "on_OK");
    } else {
      MQTT.publish(TOPIC_BOMBA, "!on");
    }
  }

  if (msg == "off" && pumpOn) {
    if (volumeTanque > MIN) {
      digitalWrite(pumpPin, LOW);
      MQTT.publish(TOPIC_BOMBA, "off_OK");
    } else {
      MQTT.publish(TOPIC_BOMBA, "!off");
    }
  }
}
