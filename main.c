#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include <string.h>
#include <stdio.h>

// Configurações da matriz NeoPixel
#include "ws2818b.pio.h"
#define LED_COUNT 10  // Total de LEDs (e vagas)
#define LED_PIN 7

typedef struct { uint8_t G, R, B; } pixel_t;
pixel_t leds[LED_COUNT];
PIO np_pio;
uint sm;

// Estados das vagas (0 = Livre, 1 = Ocupada)
uint8_t vagas_status[LED_COUNT] = {0}; // 10 vagas

// Credenciais Wi-Fi
#define WIFI_SSID "SSID"
#define WIFI_PASS "PASSWORD"

// ====================== FUNÇÕES DA MATRIZ LED ======================
void npInit(uint pin) {
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
    sm = pio_claim_unused_sm(np_pio, true);
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
    
    for(uint i = 0; i < LED_COUNT; i++) {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

void npWrite() {
    for(uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100);
}

void atualizar_matriz_leds() {
    for(int i = 0; i < LED_COUNT; i++) {
        leds[i].R = vagas_status[i] ? 255 : 0;
        leds[i].G = vagas_status[i] ? 0 : 255;
        leds[i].B = 0;
    }
    npWrite();
}

// ====================== FUNÇÕES DO SERVIDOR WEB ======================
int contar_vagas_livres() {
    int count = 0;
    for(int i = 0; i < LED_COUNT; i++) {
        if(vagas_status[i] == 0) count++;
    }
    return count;
}

char* gerar_linhas_tabela() {
    static char buffer[2048];
    buffer[0] = '\0';
    
    for(int i = 0; i < LED_COUNT; i++) {
        char linha[200];
        snprintf(linha, sizeof(linha),
            "<tr><td>Vaga %d</td><td class='%s'>%s</td>"
            "<td><button onclick=\"window.location.href='/vaga%d/toggle'\">Alternar</button></td></tr>",
            i+1,
            vagas_status[i] ? "ocupada" : "livre",
            vagas_status[i] ? "🚗 Ocupada" : "🅿️ Livre",
            i+1);
        
        strncat(buffer, linha, sizeof(buffer) - strlen(buffer) - 1);
    }
    return buffer;
}

static err_t http_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if(p == NULL) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *request = (char *)p->payload;
    for(int i = 0; i < LED_COUNT; i++) {
        char path[24];
        snprintf(path, sizeof(path), "GET /vaga%d/toggle", i+1);
        if(strstr(request, path)) {
            vagas_status[i] = !vagas_status[i];
            atualizar_matriz_leds();
        }
    }

    char response[2048];
    snprintf(response, sizeof(response), 
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<style>"
        "body { font-family: Arial, sans-serif; margin: 20px; }"
        "table { width: 100%%; border-collapse: collapse; }"
        "th, td { padding: 10px; border: 1px solid #ddd; text-align: center; }"
        ".livre { background: #90EE90; }"
        ".ocupada { background: #FFB6C1; }"
        "</style></head>"
        "<body>"
        "<h1>🅿️ Estacionamento (10 Vagas)</h1>"
        "<p>Vagas livres: <strong>%d</strong></p>"
        "<table>"
        "<tr><th>Vaga</th><th>Status</th><th>Ação</th></tr>"
        "%s"
        "</table>"
        "</body></html>",
        contar_vagas_livres(), gerar_linhas_tabela());

    tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_callback);
    return ERR_OK;
}

void start_http_server() {
    struct tcp_pcb *pcb = tcp_new();
    tcp_bind(pcb, IP_ADDR_ANY, 80);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP iniciado!\n");
}

// ====================== MAIN ======================
int main() {
    stdio_init_all();
    sleep_ms(2000);

    npInit(LED_PIN);
    atualizar_matriz_leds();

    if(cyw43_arch_init()) return 1;
    cyw43_arch_enable_sta_mode();
    
    printf("Conectando ao Wi-Fi...\n");
    if(cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Falha na conexão!\n");
        return 1;
    }
    
    printf("Conectado! IP: %s\n", ip4addr_ntoa(&cyw43_state.netif[0].ip_addr));
    start_http_server();

    while(true) {
        cyw43_arch_poll();
        sleep_ms(100);
    }

    cyw43_arch_deinit();
    return 0;
}