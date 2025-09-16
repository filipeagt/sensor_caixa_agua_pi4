#include <ESP8266WiFi.h>
#include <PubSubClient.h>

//WiFi
const char* SSID = "T350";            // SSID / nome da rede WiFi que deseja se conectar
const char* PASSWORD = "vasquinhos";  // Senha da rede WiFi que deseja se conectar
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
char vazao[5];
int level = 0;
int distFundo = 100;  //Distancia em cm em que o sensor está instalado do fundo da caixa

//Conexões sensores e bomba
#define echoPin 3  //Conectar por um resistor de 1k para limitar a corrente no boot, ao ligar o pino3 inicia em high
#define trigPin 1  //0,1 e 2 se conecatdos a LOW na inicialização da falha no boot, então o esp não iniciliza se ligados no echo
#define pumpPin 0  //Saída que controla o relê da bomba

int pumpOn = 0;        //Variável para armazenar o estado da bomba
int volumeTanque = 0;  // Volume atual (L)
// Volumes mínimo e máximo em litros
const int MIN = 250;
const int MAX = 950;

long duration;  //tempo que q onda trafega
int distance;   //distância medida

unsigned long tempoAnterior = 0;  //Váriavel que armazena o tempo em ms desde que o programa está rodando
const long intervalo = 10000;     //10 segundos

//definicao do pino do sensor e de interrupcao
const int PINO_SENSOR = 2;

//definicao da variavel de contagem de voltas
unsigned long contador = 0;

//definicao do fator de calibracao para conversao do valor lido
const float FATOR_CALIBRACAO = 4.5;

//definicao das variaveis de fluxo e volume
float fluxo = 0;
float volume = 0;
float volume_total = 0;

//definicao da variavel de intervalo de tempo
unsigned long tempo_antes = 0;

//Declaração das Funções
void mantemConexoes();  //Garante que as conexoes com WiFi e MQTT Broker se mantenham ativas
void conectaWiFi();     //Faz conexão com WiFi
void conectaMQTT();     //Faz conexão com Broker MQTT
void medeDistancia();
void enviaDados();
void calculaVolume();
void controlaBomba();
void recebePacote(char* topic, byte* payload, unsigned int length);

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(pumpPin, OUTPUT);

  //configuracao do pino do sensor como entrada em nivel logico alto
  pinMode(PINO_SENSOR, INPUT_PULLUP);

  //Serial.begin(115200);

  conectaWiFi();
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
  MQTT.setCallback(recebePacote);
}

void loop() {
  unsigned long tempoAtual = millis();

  mantemConexoes();
  //executa a contagem de pulsos uma vez por segundo
  if ((millis() - tempo_antes) >= 1000) {

    //desabilita a interrupcao para realizar a conversao do valor de pulsos
    detachInterrupt(digitalPinToInterrupt(PINO_SENSOR));

    //conversao do valor de pulsos para L/min
    fluxo = ((1000.0 / (millis() - tempo_antes)) * contador) / FATOR_CALIBRACAO;

    //exibicao do valor de fluxo
    /*Serial.print("Fluxo de: ");
    Serial.print(fluxo);
    Serial.println(" L/min");*/

    //calculo do volume em L passado pelo sensor
    volume = fluxo / 60;

    //armazenamento do volume
    volume_total += volume;

    //exibicao do valor de volume
    /*Serial.print("Volume: ");
    Serial.print(volume_total);
    Serial.println(" L");
    Serial.println();*/

    //reinicializacao do contador de pulsos
    contador = 0;

    //atualizacao da variavel tempo_antes
    tempo_antes = millis();

    //contagem de pulsos do sensor
    attachInterrupt(digitalPinToInterrupt(PINO_SENSOR), contador_pulso, FALLING);
  }
  if (tempoAtual - tempoAnterior >= intervalo) {  //Envia  o dado de acordo com o intervalo definido
    tempoAnterior = tempoAtual;
    calculaVolume();
    controlaBomba();
    enviaDados();
  }

  MQTT.loop();
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
  itoa(fluxo, vazao, 10);
  MQTT.publish(TOPIC_VAZAO, vazao);
}

//funcao chamada pela interrupcao para contagem de pulsos
void contador_pulso() {

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
  float r = 0.57;
  float R = r + 0.18 * level;
  volumeTanque = round(0.333 * 3.14 * level * (R * R + r * r + R * r));
}

void controlaBomba() {
  pumpOn = digitalRead(pumpPin);
  if (volumeTanque <= MIN && !pumpOn) {
    digitalWrite(pumpPin, HIGH);
  } else if (volumeTanque >= MAX && pumpOn) {
    digitalWrite(pumpPin, LOW);
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
  // Controla a bomba evitando erros do operador
  if (msg == "on" && !pumpOn && volume < MAX) {
    digitalWrite(pumpPin, HIGH);
  }
  if (msg == "off" && pumpOn && volume > MIN) {
    digitalWrite(pumpPin, LOW);
  }
}
