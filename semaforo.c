#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "semaforo.pio.h"

// Definicoes de pinos e constantes
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define led1 11
#define led2 13
#define botaoA 5
#define botaoB 6
#define BUZZER_PIN 21
#define LED_MATRIX_PIN 7

// Estrutura para configuracao do PIO
typedef struct { PIO pio; uint sm; } PioConfig;

// Enumeracao para os estados do semaforo
typedef enum {
    ABERTO, 
    AMARELO,
    FECHADO,
    NOTURNO
} EstadoSemaforo;

// Variaveis globais de controle
bool modo_noturno = false;
EstadoSemaforo estado_atual = FECHADO;

// Declaracoes das tasks do FreeRTOS
void vModeButtonTask();
void vTrafficLightTask();
void vBuzzerTask();
void vDisplay3Task();
void vMatrixTask();

// Ativa modo BOOTSEL ao pressionar botao B
#include "pico/bootrom.h"
void gpio_irq_handler(uint gpio, uint32_t events) {
    reset_usb_boot(0, 0);
}

int main() {
    // Inicializa botao B para ativar BOOTSEL
    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Desliga buzzer inicialmente
    pwm_set_gpio_level(BUZZER_PIN, 0);
    stdio_init_all();

    // Cria tasks
    xTaskCreate(vModeButtonTask, "Modo Noturno", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(vTrafficLightTask, "Semaforo", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vBuzzerTask, "Buzzer", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vDisplay3Task, "Display OLED", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vMatrixTask, "Matriz LED", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);

    vTaskStartScheduler();
    panic_unsupported();
}

// Task que alterna entre modo normal e noturno
void vModeButtonTask() {
    gpio_init(botaoA);
    gpio_set_dir(botaoA, GPIO_IN);
    gpio_pull_up(botaoA);
    uint32_t last_press_time = 0;

    while (true) {
        if (gpio_get(botaoA) == 0) {
            if (xTaskGetTickCount() - last_press_time > pdMS_TO_TICKS(50)) {
                modo_noturno = !modo_noturno;
                last_press_time = xTaskGetTickCount();
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Task que gerencia o semaforo
void vTrafficLightTask() {
    gpio_init(led1);
    gpio_set_dir(led1, GPIO_OUT);
    gpio_init(led2);
    gpio_set_dir(led2, GPIO_OUT);

    while (true) {
        if (!modo_noturno) {
            // Estado ABERTO
            gpio_put(led2, false);
            gpio_put(led1, true);
            estado_atual = ABERTO;
            for (int i = 0; i < 50 && !modo_noturno; i++) vTaskDelay(pdMS_TO_TICKS(100));

            // Estado AMARELO
            gpio_put(led2, true);
            estado_atual = AMARELO;
            for (int i = 0; i < 30 && !modo_noturno; i++) vTaskDelay(pdMS_TO_TICKS(100));

            // Estado FECHADO
            gpio_put(led1, false);
            estado_atual = FECHADO;
            for (int i = 0; i < 50 && !modo_noturno; i++) vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            // Estado NOTURNO (pisca os LEDs)
            estado_atual = NOTURNO;
            gpio_put(led1, true);
            gpio_put(led2, true);
            for (int i = 0; i < 15 && modo_noturno; i++) vTaskDelay(pdMS_TO_TICKS(100));
            gpio_put(led1, false);
            gpio_put(led2, false);
            for (int i = 0; i < 20 && modo_noturno; i++) vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Task responsavel pelos sons do buzzer
void vBuzzerTask() {
    uint buzzer_slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 4.0f);
    pwm_config_set_wrap(&cfg, 15625);
    pwm_init(buzzer_slice, &cfg, true);

    static EstadoSemaforo last_estado = NOTURNO;

    while (true) {
        EstadoSemaforo estado_atual_local = estado_atual;

        if (!modo_noturno) {
            // Alerta sonoro baseado no estado
            switch (estado_atual_local) {
                case ABERTO:
                    if (last_estado != ABERTO) {
                        pwm_set_gpio_level(BUZZER_PIN, 7812);
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        pwm_set_gpio_level(BUZZER_PIN, 0);
                    }
                    break;
                case AMARELO:
                    pwm_set_gpio_level(BUZZER_PIN, 7812);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    pwm_set_gpio_level(BUZZER_PIN, 0);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    break;
                case FECHADO:
                    pwm_set_gpio_level(BUZZER_PIN, 7812);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    pwm_set_gpio_level(BUZZER_PIN, 0);
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    break;
                default:
                    break;
            }
            last_estado = estado_atual_local;
        } else {
            // Modo noturno: buzzer com padrão fixo
            pwm_set_gpio_level(BUZZER_PIN, 7812);
            vTaskDelay(pdMS_TO_TICKS(1000));
            pwm_set_gpio_level(BUZZER_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

// Task para mostrar estado atual no display OLED
void vDisplay3Task() {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    const char* nomes_estados[] = { "ABERTO", "AMARELO", "FECHADO", "NOTURNO" };
    bool cor = true;

    while (true) {
        ssd1306_fill(&ssd, !cor);
        ssd1306_draw_string(&ssd, "Semaforo", 8, 6);
        ssd1306_draw_string(&ssd, nomes_estados[estado_atual], 10, 28);
        ssd1306_send_data(&ssd);
        sleep_ms(735);
    }
}

// Task que desenha estado do semaforo na matriz LED
void vMatrixTask(){
    PioConfig led_cfg;
    led_cfg.pio = pio0;
    led_cfg.sm = pio_claim_unused_sm(led_cfg.pio, true);
    uint offset = pio_add_program(led_cfg.pio, &pio_matrix_program);
    pio_matrix_program_init(led_cfg.pio, led_cfg.sm, offset, LED_MATRIX_PIN);

    while (true){
        for(int r = 0; r < 5; r++) {
            for(int c = 0; c < 5; c++) {
                uint32_t color = 0x00000000;
                
                switch(estado_atual) {
                    case ABERTO: // Seta verde para cima
                    if (
                        (r == 4 && c == 2) ||                      // ponta da seta
                        (r == 3 && (c == 1 || c == 2 || c == 3)) ||// base da seta
                        ((r >= 0 && r <= 2) && c == 2)             // haste da seta
                    ) {
                        color = 0xFF000000; // Verde
                    }
                    break;
                        
                    case AMARELO: // Quadrado nas bordas
                        if((r == 0 || r == 4) || (c == 0 || c == 4)) {
                            color = 0x3FFF0000; // Amarelo
                        }
                        break;
                        
                    case FECHADO: // X vermelho
                        if(r == c || r + c == 4) {
                            color = 0x00FF0000; // Vermelho
                        }
                        break;
                }
                
                pio_sm_put_blocking(led_cfg.pio, led_cfg.sm, color);
            }
           }
           vTaskDelay(pdMS_TO_TICKS(100));
    }
}
