#include <xc.h>                         // Incluye definiciones del PIC y registros (TRISx, LATx, ADCONx, TMRx, etc.). Sin esto no puedes usar los registros del microcontrolador.

#include <stdio.h>                      // Incluye soporte para funciones tipo printf. Aqu? se usa para enviar texto por serial (USART) con printf().

#define _XTAL_FREQ 1000000              // Define Fosc = 1 MHz para que __delay_ms() y __delay_us() calculen tiempos correctos.

#include "LibLCDXC8_1.h"                // Incluye tu librer?a del LCD (funciones como ConfiguraLCD, InicializaLCD, MensajeLCD_Var, DireccionaLCD, CrearCaracter, etc.).

#pragma config FOSC=INTOSC_EC           // Configuraci?n: usa oscilador interno del PIC (INTOSC). El _EC deja OSC2 disponible como salida clock/funci?n seg?n configuraci?n del PIC.
#pragma config WDT=OFF                  // Desactiva el Watchdog Timer: evita reinicios autom?ticos si el programa tarda o ?se cuelga?.
#pragma config LVP=OFF                  // Desactiva programaci?n en bajo voltaje: evita conflictos y libera el pin asociado para uso normal.


// ============================== CARACTERES PERSONALIZADOS LCD ==============================

unsigned char Estrella[8] = {           // Arreglo de 8 filas (LCD 5x8): define un car?cter custom que se guardar? en la CGRAM del LCD.
    0b00100,                            // Fila 1: un punto centrado.
    0b01110,                            // Fila 2: 3 puntos centrados (forma de estrella).
    0b11111,                            // Fila 3: 5 puntos (barra completa).
    0b01110,                            // Fila 4: 3 puntos.
    0b11111,                            // Fila 5: 5 puntos.
    0b01110,                            // Fila 6: 3 puntos.
    0b00100,                            // Fila 7: un punto centrado.
    0b00000                             // Fila 8: vac?o.
};

unsigned char Marco[8] = {              // Arreglo 5x8 que dibuja un ?rect?ngulo/marco? para encuadrar la entrada de datos (d?gitos) en pantalla.
    0b11111,                            // Borde superior: lleno.
    0b10001,                            // Lados: solo extremos prendidos.
    0b10001,                            // Lados.
    0b10001,                            // Lados.
    0b10001,                            // Lados.
    0b10001,                            // Lados.
    0b10001,                            // Lados.
    0b11111                             // Borde inferior: lleno.
};


// ============================== VARIABLES GLOBALES (CONSISTENTE CON GU?A 4) ==============================

unsigned int piezasTotalesContadas;     // Conteo global: cu?ntas piezas se han contado en total desde el ?ltimo reinicio del conteo.
unsigned int unidades7Seg;              // Unidades (0 a 9) que se env?an al puerto del display 7 segmentos.
unsigned int decenasRGB;                // Decenas (0 a 5) que se representan con colores del LED RGB (cada decena cambia color).

unsigned char indiceDigitoObjetivo;     // ?ndice de digitaci?n: 0 significa ?voy a escribir la decena?, 1 significa ?voy a escribir la unidad?.
unsigned char modoEdicionObjetivo;      // Bandera de edici?n: 1 = el usuario est? escribiendo el objetivo; 0 = no se acepta escritura.
unsigned int piezasObjetivo;            // Meta (objetivo) que el usuario ingresa con el teclado (ej: 25 significa contar 25 piezas).

unsigned char flagConteoActivo;         // Control de flujo: 1 = estoy contando; 0 = salgo del ciclo de conteo y vuelvo a pedir objetivo.
unsigned char teclaLeida;               // ?ltima tecla detectada del teclado matricial (n?mero o '*' como OK).
unsigned char pulsadorListo;            // Antirrebote del sensor/pulsador RC1: 1 = listo para detectar nueva pulsaci?n; 0 = ya detect? bajada, espero subida.

unsigned char segundosSinActividad;     // Inactividad: contador de segundos sin interacci?n, incrementado por Timer1 para apagar luz / Sleep.


// ============================== NUEVO EN GU?A 5: ADC + SERIAL + MOTOR ==============================

unsigned int adcValor;                  // Valor le?do del ADC. En PIC18F4550 t?pico ADC es 10 bits -> rango 0 a 1023 (dependiendo de justificaci?n y lectura ADRES).
unsigned char rxByte;                   // ?ltimo byte recibido por serial USART (caracter ASCII recibido desde PC/terminal/etc).

unsigned char paradaEmergencia;         //
unsigned char ordenMotor;               //


// ============================== PROTOTIPOS DE FUNCIONES ==============================

void __interrupt() ISR(void);           // Prototipo de la rutina de interrupciones: aqu? se atienden Timer0, Timer1, cambio en PORTB y recepci?n serial.

void ConfigVariables(void);             // Prototipo: funci?n que deja todas las variables en valores iniciales (estado conocido).
void Bienvenida(void);                  // Prototipo: funci?n que inicializa LCD y muestra mensaje de bienvenida con estrella.
void PreguntaAlUsuario(void);           // Prototipo: funci?n que pide al usuario la meta por teclado y no sale hasta tener un valor v?lido.
void ConfigPregunta(void);              // Prototipo: arma el n?mero de dos d?gitos del objetivo a partir de teclas presionadas.
void Borrar(void);                      // Prototipo: borra la meta escrita (cuando el usuario presiona SUPR).

unsigned int Conversion(unsigned char); // Prototipo: realiza una conversi?n ADC en el canal dado y retorna el resultado.
void putch(char);                       // Prototipo: funci?n necesaria para que printf env?e caracteres por UART (USART).


// ============================== FUNCI?N PRINCIPAL ==============================

void main(void){                        // Inicio del programa principal.

    ConfigVariables();                  // Inicializa variables globales (contadores, banderas, etc.) para arrancar en estado limpio.
    modoEdicionObjetivo = 0;            // Asegura que NO se pueda escribir objetivo hasta que se entre expl?citamente a PreguntaAlUsuario.
    rxByte = 0;                         // Inicializa el ?ltimo byte recibido a 0 (sin comando recibido todav?a).

    // ===================== CONFIGURACI?N DE ENTRADAS/SALIDAS Y ANAL?GICOS =====================

    ADCON1 = 0b001110;                  // Configura qu? pines son anal?gicos/digitales. Aqu? la intenci?n es dejar AN0 (RA0) anal?gico y el resto digital.
                                        // Esto es clave porque el ADC solo funciona en pines configurados como anal?gicos. Si un pin es digital, el ADC no mide bien.

    // ===================== CONFIGURACI?N DEL ADC (NUEVO EN GU?A 5) =====================

    ADCON0 = 0b00000001;                // ADCON0: selecciona canal y enciende ADC.
                                        // Bit ADON=1 -> m?dulo ADC encendido.
                                        // CHS bits en 0 -> canal AN0 seleccionado (t?picamente RA0).

    ADCON2 = 0b10001000;                // ADCON2: formato del resultado y temporizaci?n.
                                        // ADFM=1 (bit7) -> resultado justificado a la derecha (m?s c?modo para leer como n?mero normal).
                                        // ACQT y ADCS ajustan tiempo de adquisici?n y reloj del ADC (tu valor define una combinaci?n espec?fica).

    // ===================== LED RGB EN PORTE (RE0, RE1, RE2) =====================

    TRISE = 0;                          // TRIS=0 significa salida. Esto pone RE0, RE1, RE2 como SALIDAS para controlar el LED RGB.
    LATE  = 0b00000111;                 // Estado inicial del RGB. Dependiendo de si tu LED es ?nodo com?n/c?todo com?n, 1 puede significar apagado o encendido.

    // ===================== DISPLAY 7 SEGMENTOS EN PORTD =====================

    TRISD = 0;                          // Puerto D como salida para manejar el display de 7 segmentos (o su decodificador).
    LATD  = unidades7Seg;               // Muestra inicialmente el valor de unidades7Seg (normalmente 0 reci?n inicializado).

    // ===================== LED DE OPERACI?N (RA1) =====================

    TRISA1 = 0;                         // RA1 como salida digital (LED de operaci?n parpadeante).
    LATA1  = 0;                         // LED inicialmente apagado.

    // ===================== BUZZER / LED DE AVISO (RA2) =====================

    TRISA2 = 0;                         // RA2 como salida digital (buzzer o LED).
    LATA2  = 0;                         // Inicialmente apagado.

    // ===================== CONTROL DEL LCD (seg?n tu librer?a) =====================

    TRISA3 = 0;                         // RA3 como salida (en tu proyecto lo usas tambi?n como ?luz? o control de backlight, seg?n montaje).
    LATA3  = 0;                         // Estado inicial en 0.
    TRISA4 = 0;                         // RA4 como salida (frecuentemente pin RS del LCD en muchas librer?as; depende de tu configuraci?n interna).

    // ===================== MOTOR EN RC2 (NUEVO EN GU?A 5) =====================

    TRISC2 = 0;                         // RC2 como salida digital: este pin enciende/apaga el motor (probablemente a trav?s de un transistor/driver).
    LATC2  = 0;                         // Motor inicialmente apagado por seguridad.

    // ===================== USART SERIAL (NUEVO EN GU?A 5) =====================

    TRISC6 = 0;                         // RC6 = TX como salida (l?nea de transmisi?n).
    TRISC7 = 1;                         // RC7 = RX como entrada (l?nea de recepci?n).

    TXSTA  = 0b00100100;                // Registro de estado/control del transmisor USART (TXSTA).
                                        // TXEN=1 habilita el transmisor.
                                        // SYNC=0 pone modo as?ncrono.
                                        // BRGH=1 alta velocidad (impacta c?lculo del baud rate).
                                        // (Los dem?s bits quedan seg?n tu configuraci?n.)

    RCSTA  = 0b10010000;                // Registro de estado/control del receptor USART (RCSTA).
                                        // SPEN=1 habilita el puerto serial (activa RC6/RC7 como UART).
                                        // CREN=1 habilita recepci?n continua.

    BAUDCON= 0b00001000;                // BAUDCON controla el generador de baud.
                                        // BRG16=1 -> usa baud generator de 16 bits (SPBRGH:SPBRG). En tu caso no usas SPBRGH, pero dejas BRG16 activo.

    SPBRG  = 25;                        // Valor del generador de baud para aproximar 9600 bps con Fosc=1MHz y BRGH=1.
                                        // F?rmula t?pica: SPBRG = (Fosc/(4*BAUD)) - 1. Con 1MHz y 9600 da ~25.

    // ===================== ENTRADA DEL SENSOR/PULSADOR DE CONTEO (RC1) =====================

    TRISC1 = 1;                         // RC1 como entrada digital. Aqu? conectas sensor/pulsador que genera el evento de ?contar una pieza?.

    // ===================== BACKLIGHT LCD EN RA5 =====================

    TRISA5 = 0;                         // RA5 como salida digital para encender/apagar el backlight del LCD (seg?n tu circuito).
    LATA5  = 1;                         // Backlight inicialmente encendido (si tu l?gica es activa con 1). Esto depende de tu driver.

    // ===================== CONFIGURACI?N DE INTERRUPCIONES =====================

    T0CON  = 0b00000001;                // Configura Timer0. Modo 16 bits + prescaler seg?n bits. Lo usas para parpadeo y tareas peri?dicas.
    TMR0   = 3036;                      // Precarga Timer0 para que desborde aproximadamente cada cierto tiempo (en tu caso lo usas como ?tick?).
    TMR0IF = 0;                         // Limpia bandera de interrupci?n de Timer0.
    TMR0IE = 1;                         // Habilita interrupci?n de Timer0.
    TMR0ON = 1;                         // Enciende Timer0.

    T1CON  = 0b10110001;                // NUEVO: configura Timer1 para medir segundos de inactividad.
                                        // RD16=1 (lectura/escritura 16 bits), prescaler 1:8, reloj interno (Fosc/4), Timer1 ON.

    TMR1   = 3036;                      // Valor inicial (precarga). En tu ISR realmente recargas con 34286 para aproximar 1 segundo (esa es la importante).
    TMR1IF = 0;                         // Limpia bandera de Timer1.
    TMR1IE = 1;                         // Habilita interrupci?n de Timer1.
    TMR1ON = 1;                         // Enciende Timer1.

    TRISB  = 0b11110000;                // Teclado matricial: RB0-RB3 salidas (filas), RB4-RB7 entradas (columnas).
    LATB   = 0b00000000;                // Inicializa filas en 0.
    RBPU   = 0;                         // Activa pull-ups internos en PORTB (para columnas en 1 cuando no se presiona nada).
    __delay_ms(100);                    // Delay de estabilizaci?n para que entradas no queden flotantes justo al arrancar.
    RBIF   = 0;                         // Limpia bandera de cambio en PORTB.
    RBIE   = 1;                         // Habilita interrupci?n por cambio en RB4-RB7.

    RCIF   = 0;                         // Limpia bandera de recepci?n serial (RCIF) antes de empezar (seguridad).
    RCIE   = 1;                         // Habilita interrupci?n de recepci?n serial: cuando llegue un byte, entra a ISR.

    PEIE   = 1;                         // Habilita interrupciones de perif?ricos (Timer0, Timer1, USART, etc.).
    GIE    = 1;                         // Habilita interrupciones globales (si esto est? en 0, no entra a ISR).

    // ===================== INICIO: LCD Y MENSAJES DE RESET (NUEVO EN GU?A 5) =====================

    LATA3 = 1;                          // Enciende ?luz? asociada a RA3 (en tu montaje lo usas como indicador/backlight alterno).
    Bienvenida();                       // Muestra el mensaje de bienvenida usando Estrella (car?cter CGRAM 0).

    if(POR == 0){                       // NUEVO: revisa si el reset se asocia a Power-On Reset (POR). Este bit es una bandera del hardware del PIC.
        POR = 1;                        // Limpia la bandera POR para futuras detecciones (no confundir eventos).
        BorraLCD();                     // Limpia LCD.
        OcultarCursor();                // Oculta cursor.
        MensajeLCD_Var("    FALLA DE"); // Mensaje: primera l?nea.
        DireccionaLCD(0xC0);            // Cursor al inicio de segunda l?nea.
        MensajeLCD_Var("     ENERGIA"); // Mensaje: segunda l?nea.
    }else{                              // Caso contrario: se interpreta como reset no-POR (por ejemplo reset manual).
        BorraLCD();                     // Limpia LCD.
        OcultarCursor();                // Oculta cursor.
        MensajeLCD_Var("    RESET DE"); // Mensaje: primera l?nea.
        DireccionaLCD(0xC0);            // Cursor a segunda l?nea.
        MensajeLCD_Var("     USUARIO"); // Mensaje: segunda l?nea.
    }

    __delay_ms(1000);                   // Pausa para que el mensaje se vea 1 segundo.
    LATA3 = 0;                          // Apaga ?luz? en RA3 despu?s del aviso.

    // ===================== LOOP PRINCIPAL =====================

    while(1){                           // Bucle infinito: el sistema corre siempre.

        PreguntaAlUsuario();            // Pide al usuario el objetivo con teclado (usa Marco, dos d?gitos + OK). Se queda aqu? hasta que salga con objetivo v?lido.
        OcultarCursor();                // Oculta el cursor despu?s de terminar la digitaci?n.

        MensajeLCD_Var("Faltantes: ");  // Muestra texto ?Faltantes:? en primera l?nea.
        EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2); // Escribe cu?ntas faltan (2 d?gitos).
        DireccionaLCD(0xC0);            // Salta a segunda l?nea.
        MensajeLCD_Var("Objetivo: ");   // Muestra texto ?Objetivo:? en segunda l?nea.
        EscribeLCD_n8(piezasObjetivo, 2); // Escribe el valor objetivo (2 d?gitos).

        flagConteoActivo = 1;           // Activa el modo conteo: permite entrar al while interno de conteo.

        while(flagConteoActivo == 1){  // Mientras estemos contando, se eval?an eventos (cumplir meta, pulsador RC1).

            if(piezasTotalesContadas == piezasObjetivo){ // Caso: ya alcanzamos el objetivo.

                LATA2 = 1;              // Activa buzzer/LED de aviso.
                __delay_ms(1000);       // Mantiene 1 segundo para indicar ?cumplido?.
                LATA2 = 0;              // Apaga buzzer/LED.

                BorraLCD();             // Limpia pantalla.
                MensajeLCD_Var("Cuenta Cumplida"); // Texto de ?xito.
                DireccionaLCD(0xC0);    // Segunda l?nea.
                MensajeLCD_Var("   Presione OK"); // Indicaci?n al usuario.

                flagConteoActivo = 0;   // Sale del modo conteo (romper? el while interno).
                teclaLeida = '\0';      // Limpia la tecla anterior.

                while(teclaLeida != '*'){} // Espera hasta que el teclado mande '' (OK) a trav?s de la ISR del PORTB.

                ConfigVariables();      // Reinicia variables para comenzar de nuevo desde cero.
            }

            if(RC1 == 0 && piezasTotalesContadas != piezasObjetivo){ // Si RC1 est? presionado (activo en bajo) y a?n no llegamos al objetivo.
                segundosSinActividad = 0; // Se reinicia inactividad: porque el usuario/sensor est? generando actividad real.
                pulsadorListo = 0;        // Marca que se detect? la bajada y ahora se espera la subida (antirrebote por flanco).
            }

            if(pulsadorListo == 0){       // Si estamos esperando la subida...
                if(RC1 == 1){             // Si RC1 volvi? a 1, eso significa que se complet? un pulso v?lido (flanco de subida).

                    pulsadorListo = 1;    // Se bloquea para no contar otra vez hasta una nueva bajada.

                    unidades7Seg++;       // Aumenta las unidades del conteo (0?9) para el display.
                    piezasTotalesContadas++; // Aumenta el total global de piezas.

                    if(unidades7Seg == 10){ // Si pasamos de 9 a 10, entonces se completa una decena.

                        LATA2 = 1;        // Aviso corto por decena.
                        __delay_ms(300);  // Duraci?n del aviso.
                        LATA2 = 0;        // Apaga aviso.

                        unidades7Seg = 0; // Reinicia unidades.
                        decenasRGB++;     // Incrementa decenas, que se ?ver?n? por el color del RGB.

                        if(decenasRGB == 6){ // Si llega a 6, se reinicia (porque tu esquema de colores/m?ximo era hasta 59).

                            LATA2 = 1;    // Segundo aviso (opcional) que t? ten?as cuando reinicia.
                            __delay_ms(300);
                            LATA2 = 0;

                            decenasRGB = 0; // Reinicia decenas.
                        }
                    }

                    if(decenasRGB == 0){
                        LATE = 0b00000001; // Magenta (Rojo+Azul) seg?n tu conexi?n.
                    }else if(decenasRGB == 1){
                        LATE = 0b00000101; // Azul
                    }else if(decenasRGB == 2){
                        LATE = 0b00000100; // Cyan
                    }else if(decenasRGB == 3){
                        LATE = 0b00000110; // Verde
                    }else if(decenasRGB == 4){
                        LATE = 0b00000010; // Amarillo
                    }else if(decenasRGB == 5){
                        LATE = 0b00000000; // Blanco
                    }

                    DireccionaLCD(0x8B);      // Posiciona cursor donde est?s mostrando el n?mero de ?faltantes? en la primera l?nea.
                    EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2); // Actualiza faltantes.

                    LATD = unidades7Seg;      // Actualiza el display de 7 segmentos con las unidades actuales.

                    __delay_ms(500);          // Retardo adicional para que un pulso mec?nico no cuente varias veces (antirrebote extra).
                }
            }
        }

        LATE = 0b00000010;                // Al salir del conteo, dejas el RGB en estado base (tu color ?reposo?).
        LATD = unidades7Seg;              // Mantienes visualmente el ?ltimo valor en el 7 segmentos.
    }
}


// ============================== ISR (RUTINA DE INTERRUPCIONES) ==============================

void __interrupt() ISR(void){            // Funci?n llamada autom?ticamente cuando ocurre una interrupci?n habilitada.

    // ===================== NUEVO EN GU?A 5: INTERRUPCI?N POR RECEPCI?N SERIAL =====================

    if(RCIF == 1){                       // RCIF=1 significa: lleg? un byte por UART al registro RCREG.
        if(RCSTAbits.OERR == 1){
            RCSTAbits.CREN = 0;
            RCSTAbits.CREN = 1;
        }

        rxByte = RCREG;                  // Leer RCREG es obligatorio: ?consume? el byte recibido y limpia la condici?n de recepci?n.

        if(rxByte == 'P' || rxByte == 'p'){ // Si por serial llega P/p, se interpreta como PARADA DE EMERGENCIA.

            paradaEmergencia = 1;
            LATC2 = 0;

            LATE = 0b00000011;           // Coloca el RGB en rojo (seg?n tu configuraci?n f?sica del LED RGB).
            BorraLCD();                  // Limpia pantalla.
            OcultarCursor();             // Oculta cursor.
            MensajeLCD_Var("   PARADA DE"); // Mensaje l?nea 1.
            DireccionaLCD(0xC0);         // L?nea 2.
            MensajeLCD_Var("   EMERGENCIA"); // Mensaje l?nea 2.

            while(1){}                   // Bucle infinito: el sistema se ?detiene? hasta reset (seguridad).
        }
        else if(paradaEmergencia == 0 && (rxByte == 'E' || rxByte == 'e')){

            ordenMotor = 1;
            LATC2 = 1;
        }
        else if(paradaEmergencia == 0 && (rxByte == 'A' || rxByte == 'a')){

            ordenMotor = 2;
            LATC2 = 0;
        }
        else if(flagConteoActivo == 1 && (rxByte == 'R' || rxByte == 'r')){ // Si llega R/r mientras cuentas, reinicia el conteo.

            unidades7Seg = 0;            // Reinicia unidades a 0.
            piezasTotalesContadas = 0;   // Reinicia conteo global a 0.
            decenasRGB = 0;              // Reinicia decenas a 0.
            LATE = 0b00000001;           // RGB vuelve a color de reposo.

            DireccionaLCD(0x8B);         // Posiciona donde van los faltantes.
            EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2); // Actualiza faltantes (ahora ser? igual al objetivo).
            LATD = unidades7Seg;         // Display vuelve a 0.
        }
    }

    // ===================== TIMER0: PARPADEO + ADC + PRINTF + CONTROL MOTOR =====================

    if(TMR0IF == 1){                     // Si TMR0IF=1 es porque Timer0 desbord?.
        TMR0 = 3036;                     // Recarga Timer0 para mantener periodicidad.
        TMR0IF = 0;                      // Limpia bandera para poder detectar el pr?ximo desborde.

        LATA1 = LATA1 ^ 1;               // Toggle LED operaci?n (parpadeo).

        adcValor = Conversion(0);        // NUEVO: lee el ADC canal 0 (AN0/RA0). Retorna ADRES.
        printf("Valor del ADC:%d\r\n", adcValor); // NUEVO: env?a el valor por serial usando printf (putch se encarga del env?o).

        if(paradaEmergencia == 0){
            if(ordenMotor == 1){
                LATC2 = 1;
            }else if(ordenMotor == 2){
                LATC2 = 0;
            }else{
                if(adcValor >= 511){
                    LATC2 = 1;
                }else{
                    LATC2 = 0;
                }
            }
        }else{
            LATC2 = 0;
        }
    }

    // ===================== TIMER1: INACTIVIDAD + BACKLIGHT + SLEEP (NUEVO EN GU?A 5) =====================

    if(TMR1IF == 1){                     // Si TMR1IF=1, Timer1 desbord? (tick de inactividad).
        TMR1 = 34286;                    // Precarga para aproximar 1 segundo (seg?n Fosc y prescaler); esto es el ?segundo? de tu contador de inactividad.
        TMR1IF = 0;                      // Limpia bandera.

        segundosSinActividad++;          // Incrementa contador de segundos sin actividad.

        if(segundosSinActividad == 30){  // Si pasan 10 s sin actividad...
            LATA3 = 0;                   // Apaga la ?luz/backlight? controlada por RA3 (seg?n tu montaje).
        }

        if(segundosSinActividad >= 60){  // Si pasan 20 s sin actividad...
            Sleep();                     // Instrucci?n del PIC: entra en modo bajo consumo hasta que una interrupci?n lo despierte.

            segundosSinActividad = 0;    // Al despertar, reinicia el conteo de inactividad.
            RBIF = 0;                    // Limpia bandera del teclado (si despert? por ah?).
            TMR1ON = 1;                  // Asegura que Timer1 vuelva a correr tras el sleep.
        }
    }

    // ===================== TECLADO MATRICIAL: CAMBIO EN PORTB (MISMO ESQUEMA GU?A 4) =====================

    if(RBIF == 1){                       // RBIF=1 indica cambio en RB4-RB7 (columnas), t?pico cuando se presiona una tecla.
        if(PORTB != 0b11110000){         // Verifica que haya una tecla real presionada (no todo en 1).

            segundosSinActividad = 0;    // Como hubo interacci?n, reinicia inactividad.

            LATB = 0b11111110;           // Activa fila 1 (RB0=0).
            if(RB4 == 0){                // Columna 1 presionada con fila 1 activa -> tecla '1'
                teclaLeida = 1;          // Guarda 1.
                ConfigPregunta();        // Construye objetivo si estamos en modo edici?n.
            }
            else if(RB5 == 0){           // Tecla '2'
                teclaLeida = 2;
                ConfigPregunta();
            }
            else if(RB6 == 0){           // Tecla '3'
                teclaLeida = 3;
                ConfigPregunta();
            }
            else if(RB7 == 0){           // Tecla OK
                teclaLeida = '*';        // '' se usa como ?confirmar?.
            }
            else{
                LATB = 0b11111101;       // Activa fila 2 (RB1=0).
                if(RB4 == 0){            // Tecla '4'
                    teclaLeida = 4;
                    ConfigPregunta();
                }
                else if(RB5 == 0){       // Tecla '5'
                    teclaLeida = 5;
                    ConfigPregunta();
                }
                else if(RB6 == 0){       // Tecla '6'
                    teclaLeida = 6;
                    ConfigPregunta();
                }
                else if(RB7 == 0){       // Parada de emergencia por teclado.

                    LATE = 0b00000110;   // RGB rojo.
                    BorraLCD();
                    OcultarCursor();
                    MensajeLCD_Var("   PARADA DE");
                    DireccionaLCD(0xC0);
                    MensajeLCD_Var("   EMERGENCIA");
                    while(1){}
                }
                else{
                    LATB = 0b11111011;   // Activa fila 3 (RB2=0).
                    if(RB4 == 0){        // Tecla '7'
                        teclaLeida = 7;
                        ConfigPregunta();
                    }
                    else if(RB5 == 0){   // Tecla '8'
                        teclaLeida = 8;
                        ConfigPregunta();
                    }
                    else if(RB6 == 0){   // Tecla '9'
                        teclaLeida = 9;
                        ConfigPregunta();
                    }
                    else if(RB7 == 0){   // SUPR
                        Borrar();        // Borra objetivo si se est? editando.
                    }
                    else{
                        LATB = 0b11110111; // Activa fila 4 (RB3=0).
                        if(RB4 == 0){      // REINICIO de conteo.

                            unidades7Seg = 0;
                            piezasTotalesContadas = 0;
                            decenasRGB = 0;
                            LATE = 0b00000001;

                            if(flagConteoActivo == 1){
                                DireccionaLCD(0x8B);
                                EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2);
                                LATD = unidades7Seg;
                            }
                        }
                        else if(RB5 == 0){ // Tecla '0'
                            teclaLeida = 0;
                            ConfigPregunta();
                        }
                        else if(RB6 == 0){ // FIN: fuerza objetivo cumplido.

                            Borrar();
                            piezasTotalesContadas = piezasObjetivo;
                            decenasRGB = piezasObjetivo / 10;
                            unidades7Seg = piezasObjetivo - decenasRGB * 10;

                          if(decenasRGB == 0){
                        LATE = 0b00000001; // Magenta (Rojo+Azul) seg?n tu conexi?n.
                    }else if(decenasRGB == 1){
                        LATE = 0b00000101; // Azul
                    }else if(decenasRGB == 2){
                        LATE = 0b00000100; // Cyan
                    }else if(decenasRGB == 3){
                        LATE = 0b00000110; // Verde
                    }else if(decenasRGB == 4){
                        LATE = 0b00000010; // Amarillo
                    }else if(decenasRGB == 5){
                        LATE = 0b00000000; // Blanco
                    }

                            LATD = unidades7Seg;
                        }
                        else if(RB7 == 0){ // LUZ: toggle RA3 manual.
                            LATA3 = LATA3 ^ 1;
                            TMR1ON = 1;
                        }
                    }
                }
            }

            LATB = 0b11110000;           // Restablece filas a estado ?inactivo?.
        }

        __delay_ms(300);                 // Antirrebote del teclado (evita m?ltiples lecturas por una sola pulsaci?n).
        RBIF = 0;                        // Limpia bandera para permitir nuevas interrupciones por teclado.
    }
}


// ============================== FUNCIONES DE APOYO ==============================

void ConfigVariables(void){             // Funci?n de inicializaci?n de variables globales.

    pulsadorListo = 1;                  // Estado inicial: listo para detectar nueva pulsaci?n en RC1.
    unidades7Seg = 0;                   // Unidades del display en 0.
    flagConteoActivo = 0;               // No estamos contando al inicio.
    piezasTotalesContadas = 0;          // Conteo total en 0.
    decenasRGB = 0;                     // Decenas en 0.
    indiceDigitoObjetivo = 0;           // Primer d?gito al iniciar digitaci?n.
    piezasObjetivo = 0;                 // Objetivo inicial vac?o.
    teclaLeida = '\0';                  // Sin tecla v?lida al inicio.
    segundosSinActividad = 0;           // Inactividad inicia en 0.

    adcValor = 0;                       // ADC inicia en 0.
    rxByte = 0;                         // Sin comando recibido por serial.

    paradaEmergencia = 0;               //
    ordenMotor = 0;                     //
}

void Bienvenida(void){                  // Mensaje inicial en el LCD.

    ConfiguraLCD(4);                    // Configura LCD en modo 4 bits (menos pines).
    InicializaLCD();                    // Inicializa LCD (secuencia de arranque interna del controlador HD44780 o similar).
    OcultarCursor();                    // Oculta cursor para est?tica.

    CrearCaracter(Estrella, 0);         // Guarda el car?cter ?Estrella? en CGRAM posici?n 0.

    // Mensaje en pantalla con estrellas decorativas
    EscribeLCD_c(0); 
    EscribeLCD_c(0);
    MensajeLCD_Var(" Bienvenido ");
    EscribeLCD_c(0);
    EscribeLCD_c(0);
    // Imprime dos estrellas, la palabra " Bienvenido " y otras dos estrellas en la primera l?nea.

    DireccionaLCD(0xC0);
    // Mueve el cursor al inicio de la segunda l?nea.

    EscribeLCD_c(0);
    EscribeLCD_c(0);
    MensajeLCD_Var("  Operario ");
    EscribeLCD_c(0);
    EscribeLCD_c(0);
    __delay_ms(4200);                   // Pausa para que el usuario lo vea.

    for(int i = 0; i < 18; i++){        // Loop de animaci?n: desplaza pantalla a la derecha.
        DesplazaPantallaD();            // Comando del LCD para desplazar todo el display.
        __delay_ms(100);                // Retardo entre desplazamientos para ver la animaci?n.
    }
}

void PreguntaAlUsuario(void){           // Pide al usuario el objetivo a contar.

    while(1){                           // Se repite hasta que el objetivo sea v?lido.

        CrearCaracter(Marco, 1);        // Guarda el car?cter ?Marco? en CGRAM posici?n 1.
        indiceDigitoObjetivo = 0;       // Arranca digitaci?n en el primer d?gito.

        BorraLCD();                     // Limpia pantalla.
        MensajeLCD_Var("Piezas a contar:"); // Mensaje de solicitud.
        DireccionaLCD(0xC7);            // Posiciona cursor en lugar de entrada (coordenada que t? elegiste).
        EscribeLCD_c(1);                // Imprime primer Marco.
        EscribeLCD_c(1);                // Imprime segundo Marco.
        DireccionaLCD(0xC7);            // Cursor vuelve al inicio del primer d?gito.
        MostrarCursor();                // Muestra cursor para indicar ?puedes escribir?.

        modoEdicionObjetivo = 1;        // Activa modo edici?n: ConfigPregunta() ahora s? modifica piezasObjetivo.

        while(teclaLeida != '*'){       // Espera a que el usuario presione OK.
                                        // Las teclas num?ricas se procesan en la ISR, que llama ConfigPregunta().
        }

        if((piezasObjetivo > 59) || (piezasObjetivo == 0)){ // Si el valor est? fuera de rango (mismo comportamiento de tu gu?a original).

            modoEdicionObjetivo = 0;    // Desactiva edici?n.
            teclaLeida = '\0';          // Limpia tecla.
            piezasObjetivo = 0;         // Borra el objetivo armado.

            BorraLCD();                 // Limpia pantalla.
            OcultarCursor();            // Oculta cursor.
            MensajeLCD_Var("Error"); // Mensaje neutro (no ?ERROR?).
            DireccionaLCD(0xC0);        // Segunda l?nea.
            MensajeLCD_Var("Try again"); // Indicaci?n simple.
            __delay_ms(2000);           // Tiempo para leer.
            BorraLCD();                 // Limpia y vuelve a pedir.
        }else{

            modoEdicionObjetivo = 0;    // Desactiva edici?n: ya no se aceptan n?meros para objetivo.
            indiceDigitoObjetivo = 0;   // Reinicia ?ndice por limpieza.
            BorraLCD();                 // Limpia pantalla para empezar conteo.
            teclaLeida = '\0';          // Limpia tecla.
            break;                      // Sale del while(1) porque ya tenemos objetivo v?lido.
        }
    }
}

void ConfigPregunta(void){              // Construye el objetivo de dos d?gitos.

    if(indiceDigitoObjetivo == 0 && modoEdicionObjetivo == 1){ // Primer d?gito (decenas).

        EscribeLCD_n8(teclaLeida, 1);   // Escribe el d?gito en el LCD.
        piezasObjetivo = teclaLeida;    // Guarda ese d?gito como base (por ejemplo: si presiona 3, objetivo temporal = 3).
    }
    else if(indiceDigitoObjetivo == 1 && modoEdicionObjetivo == 1){ // Segundo d?gito (unidades).

        EscribeLCD_n8(teclaLeida, 1);   // Escribe el segundo d?gito en el LCD.
        piezasObjetivo = piezasObjetivo * 10 + teclaLeida; // Forma el n?mero final: (decena*10 + unidad).
        OcultarCursor();                // Oculta cursor porque ya se completaron los dos d?gitos.
    }

    indiceDigitoObjetivo++;             // Avanza al siguiente d?gito (0->1->2...). Despu?s de 2, ya no deber?a seguir escribiendo.
}

void Borrar(void){                      // Borra el objetivo digitado.

    if(modoEdicionObjetivo == 1){       // Solo borra si estamos en edici?n.

        MostrarCursor();                // Muestra cursor para indicar que se puede reescribir.
        piezasObjetivo = 0;             // Reinicia valor objetivo.
        indiceDigitoObjetivo = 0;       // Reinicia ?ndice a primer d?gito.
        DireccionaLCD(0xC7);            // Cursor al lugar de entrada.
        EscribeLCD_c(1);                // Dibuja Marco.
        EscribeLCD_c(1);                // Dibuja Marco.
        DireccionaLCD(0xC7);            // Cursor al primer d?gito.
    }
}

unsigned int Conversion(unsigned char canal){ // Conversi?n ADC: retorna lectura de ADRES.

    ADCON0 = (canal << 2);              // Selecciona el canal poniendo canal en bits CHS (se desplaza 2 posiciones porque CHS empieza en bit2).
                                        // Importante: aqu? est?s reescribiendo ADCON0, as? que si quieres conservar ADON deber?as reactivar ADON luego.

    ADON = 1;                           // Enciende el ADC (si por reescritura se apag? o para asegurar que est? encendido).
    GO_DONE = 1;                        // Inicia conversi?n (bit GO/DONE = 1).
    while(GO_DONE == 1);                // Espera a que termine la conversi?n (GO_DONE vuelve a 0 al terminar).

    return ADRES;                       // Retorna el resultado del ADC (ADRESH:ADRESL combinados).
}

void putch(char data){                  // Funci?n que usa printf para transmitir caracteres.

    while(TRMT == 0);                   // Espera hasta que el registro de transmisi?n est? vac?o (TRMT=1 indica listo para nuevo car?cter).
    TXREG = data;                       // Carga el car?cter a transmitir. USART lo enviar? por el pin TX (RC6).
}
