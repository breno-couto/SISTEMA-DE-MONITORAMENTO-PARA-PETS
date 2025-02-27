#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include <hardware/clocks.h>
#include "pico/bootrom.h"
#include "ws2818b.pio.h"

#define LED_B 12
#define LED_G 11
#define LED_R 13
#define BOTAO_JOYSTICK 22
#define BOTAO_A 5
#define joyX 26
#define joyY 27
#define LED_COUNT 25
#define LED_PIN 7
#define DEBOUNCE_TIME 200000  // 200 ms em microssegundos

// Variavel controladora para ligar e desligar leds e leituras
static bool liga_desliga = false;

// Variavel controladora para debounce do botao A
static uint32_t last_interrupt_time_A = 0;

// Variavel controladora da matriz de leds 5x5
int contador_5x5 = 1;

//definicao dos pixels
struct pixel_t{
    uint8_t G, R, B;
};

typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t;

//definicao do buffer de pixels
npLED_t leds[LED_COUNT];

PIO np_pio;
uint sm;

// Declaração das funções
void setup_pwm(uint gpio);
void set_pwm(uint gpio, uint16_t value);
void irq_callback(uint gpio, uint32_t eventos);
void full_setup();
int ler_joystick_x();
int ler_joystick_y();
void npInit(uint pin);
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b);
void npClear();
void npWrite();
int getIndex(int x, int y);
void npSetPattern(int matriz[5][5][3]);
void npSetAllRed();
float mapear(int valor, int in_min, int in_max, float out_min, float out_max);
void apagar_leds();

int main() {   
    
    full_setup();

    while (true) {

        int eixo_y = ler_joystick_y();
        int eixo_x = ler_joystick_x();

        // Mapeia os valores do joystick para os intervalos desejados
        float temperatura = mapear(eixo_y, 0, 4095, 36.0, 40.0);  // Temperatura de 36 a 40°C
        float batimentos = mapear(eixo_x, 0, 4095, 50.0, 110.0);  // Batimentos de 50 a 110 BPS

        // Normaliza valores para PWM (0 - 4095)
        uint16_t pwm_vermelho = abs(eixo_y - 2048) * 2; // Quanto mais longe do centro, mais brilho
        uint16_t pwm_azul = abs(eixo_x - 2048) * 2;

        // Aplica PWM aos LEDs
        set_pwm(LED_R, pwm_vermelho);
        set_pwm(LED_B, pwm_azul);

        if(liga_desliga){
            // Verifica se a temperatura ou os batimentos estão fora dos limites
            if (temperatura > 39.9 || temperatura < 36.1 || batimentos > 109 || batimentos < 51) {
                npSetAllRed();  // Aciona LEDs vermelhos na matriz
            } else {
                apagar_leds();
            } 
        }

        sleep_ms(10);
    }
}

// Função para mapear valores de um intervalo para outro
float mapear(int valor, int in_min, int in_max, float out_min, float out_max) {
    return out_min + (float)(valor - in_min) * (out_max - out_min) / (in_max - in_min);
}

// Configura PWM para um pino específico
void setup_pwm(uint gpio) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_wrap(slice, 4095);  // PWM de 12 bits (0-4095)
    pwm_set_enabled(slice, true);
}

// Define o nível PWM do LED
void set_pwm(uint gpio, uint16_t value) {
    pwm_set_gpio_level(gpio, liga_desliga ? value : 0);
}

// Callback única para interrupções
void irq_callback(uint gpio, uint32_t eventos) {
    uint32_t current_time = time_us_32();  // Obtém o tempo atual
        if (current_time - last_interrupt_time_A < DEBOUNCE_TIME) return;  // Ignora se for muito rápido
        last_interrupt_time_A = current_time;

        liga_desliga = !liga_desliga;
}

void full_setup() {

    stdio_init_all();

    // Inicia os leds 5x5
    npInit(LED_PIN);
    npClear();

    // Configura o ADC
    adc_init();
    adc_gpio_init(joyX);
    adc_gpio_init(joyY);

    // Configura LEDs PWM
    setup_pwm(LED_R);
    setup_pwm(LED_B);

    // Configura Botões como entrada com pull-up interno
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);

    // Configura interrupção única para ambos os botões
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &irq_callback);

}

// Lê valores do joystick (ADC)
int ler_joystick_y(){
    adc_select_input(0);
    return adc_read();
}

int ler_joystick_x(){
    adc_select_input(1);
    return adc_read();
}

void npInit(uint pin) {

    //programa pio
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
   
    //encontra pio
    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0) {
      np_pio = pio1;
      sm = pio_claim_unused_sm(np_pio, true);
    }
   
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
   
    for (uint i = 0; i < LED_COUNT; ++i) {
      leds[i].R = 0;
      leds[i].G = 0;
      leds[i].B = 0;
    }
   }
   
   void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
   }
   
   void npClear() {
    for (uint i = 0; i < LED_COUNT; ++i)
      npSetLED(i, 0, 0, 0);
   }
   
   void npWrite() {
    for (uint i = 0; i < LED_COUNT; ++i) {
      pio_sm_put_blocking(np_pio, sm, leds[i].G);
      pio_sm_put_blocking(np_pio, sm, leds[i].R);
      pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
   }
   
   int getIndex(int x, int y) {
       if (y % 2 == 0) {
           return y * 5 + x;
       } else {
           return y * 5 + (4 - x);
       }
   }
   
   void npSetPattern(int matriz[5][5][3]) {
       int index = 0;
   int conversao[5][5][3]=
   {
       {{matriz[4][4][0],matriz[4][4][1], matriz[4][4][2]},{matriz[4][3][0],matriz[4][3][1], matriz[4][3][2]}, {matriz[4][2][0],matriz[4][2][1], matriz[4][2][2]}, {matriz[4][1][0],matriz[4][1][1], matriz[4][1][2]}, {matriz[4][0][0],matriz[4][0][1], matriz[4][0][2]}},
       {{matriz[3][0][0],matriz[3][0][1], matriz[3][0][2]}, {matriz[3][1][0],matriz[3][1][1], matriz[3][1][2]}, {matriz[3][2][0],matriz[3][2][1], matriz[3][2][2]}, {matriz[3][3][0],matriz[3][3][1], matriz[3][3][2]}, {matriz[3][4][0],matriz[3][4][1], matriz[3][4][2]}},
       {{matriz[2][4][0],matriz[2][4][1], matriz[2][4][2]}, {matriz[2][3][0],matriz[2][3][1], matriz[2][3][2]}, {matriz[2][2][0],matriz[2][2][1], matriz[2][2][2]}, {matriz[2][1][0],matriz[2][1][1], matriz[2][1][2]}, {matriz[2][0][0],matriz[2][0][1], matriz[2][0][2]}},
       {{matriz[1][0][0],matriz[1][0][1], matriz[1][0][2]},{matriz[1][1][0],matriz[1][1][1], matriz[1][1][2]}, {matriz[1][2][0],matriz[1][2][1], matriz[1][2][2]}, {matriz[1][3][0],matriz[1][3][1], matriz[1][3][2]}, {matriz[1][4][0],matriz[1][4][1], matriz[1][4][2]}},
       {{matriz[0][4][0],matriz[0][4][1], matriz[0][4][2]}, {matriz[0][3][0],matriz[0][3][1], matriz[0][3][2]}, {matriz[0][2][0],matriz[0][2][1], matriz[0][2][2]}, {matriz[0][1][0],matriz[0][1][1], matriz[0][1][2]}, {matriz[0][0][0],matriz[0][0][1], matriz[0][0][2]}}
   };
   
   
       for (int linha = 0; linha < 5; linha++) {
           for (int coluna = 0; coluna < 5; coluna++) {
               int r = conversao[linha][coluna][0]; //R
               int g = conversao[linha][coluna][1]; //G
               int b = conversao[linha][coluna][2]; //B
   
               //acende o led
               npSetLED(index, r, g, b);
               index++;
           }
       }
       npWrite(); //envia dados
   }

   void npSetAllRed() {
    for (int i = 0; i < LED_COUNT; i++) {
        npSetLED(i, 255, 0, 0); // Define cada LED como vermelho (R=255, G=0, B=0)
    }
    npWrite(); // Atualiza a matriz de LEDs
}

// Função para apagar todos os LEDs da matriz (definindo RGB = 0)
void apagar_leds() {
    int matriz[5][5][3] = {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
                           {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
                           {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
                           {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
                           {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
    npSetPattern(matriz);  // Atualiza a matriz para apagá-la
    npWrite();  // Aplica a mudança
}
