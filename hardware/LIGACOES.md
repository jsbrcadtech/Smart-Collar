# Ligações testadas do MPU6050

Placa: Heltec WiFi LoRa 32 V3.2 (ESP32-S3). Sensor: GY-521 / MPU6050.

| GY-521 | Heltec |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO4 |
| SCL | GPIO5 |

O display OLED onboard usa GPIO17 (SDA), GPIO18 (SCL), GPIO21 (reset) e GPIO36 (alimentação Vext); o LED usa GPIO35 e o botão PRG GPIO0, conforme código fornecido. Não altere as ligações do protótipo validado sem atualizar a documentação.

**LoRa:** as duas placas devem ter antena adequada conectada **antes** de habilitar transmissão. Frequência padrão definida em firmware: 915 MHz; confira variante física do módulo e requisitos locais.

**Energia:** comece por USB, na bancada. Não coloque bateria na coleira de um animal sem proteção elétrica/mecânica apropriada, isolamento, alívio de tração e avaliação de fixação.
