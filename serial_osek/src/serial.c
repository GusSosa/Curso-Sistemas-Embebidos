/* Copyright 2018, Gustavo Sosa - UTN FRT
 * Copyright 2017, Esteban Volentini - Facet UNT, Fi UNER
 * Copyright 2014, Mariano Cerdeiro
 * Copyright 2014, Pablo Ridolfi
 * Copyright 2014, Juan Cecconi
 * Copyright 2014, Gustavo Muro
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/** @file serial.c
 **
 ** @brief Transmisión serial con interrupciones y FreeOSEK
 **
 ** Ejemplo de una transmisión serial utilizando interupciones y FreeOSEK. En 
 ** el mismo una tarea escanea el teclado y cuando se pulsa la tecla 1 se 
 ** activa una tarea que envia la cadena 'Hola' y una vez completa la transición
 ** de la misma se envia la cadena 'Mundo'.
 ** 
 ** | RV | YYYY.MM.DD | Autor       | Descripción de los cambios              |
 ** |----|------------|-------------|-----------------------------------------|
 ** |  3 | 2018.09.19 | gsosa       | Adaptación código final                 |
 ** |  2 | 2017.10.21 | evolentini  | Correción en el formato del archivo     |
 ** |  1 | 2017.09.16 | evolentini  | Version inicial del archivo             |
 ** 
 ** @defgroup ejemplos Proyectos de ejemplo
 ** @brief Proyectos de ejemplo de la Especialización en Sistemas Embebidos
 ** @{ 
 */

/* === Inclusiones de cabeceras ============================================ */
#include <stdint.h>
#include <string.h>
#include "serial.h"
#include "led.h"
#include "switch.h"
#include "uart.h"
#include "os.h"

/* === Definicion y Macros ================================================= */

/* === Declaraciones de tipos de datos internos ============================ */

/** @brief Estructura de datos para transmisión
 ** 
 ** Estructura que contiene los datos necesarios para que la interrupción 
 ** pueda continuar el envio de datos usando la función @ref EnviarCaracter
 ** de la transmisisión iniciada por la función @ref EnviarTexto.
 */
typedef struct {
   const char * datos;   /** < Puntero al bloque de datos a enviar */
   uint8_t cantidad;     /** < Cantidad de datos a enviar */
   uint8_t enviados;     /** < Cantidad de datos enviados */
} cola_t;

/* === Declaraciones de funciones internas ================================= */

/** @brief Envio de una cadena por puerto serial
 **
 ** Esta función comienza el envio de una cadena serial por el puerto de la 
 ** uart conectada a la interface de depuracion USB. La función es no blocante
 ** y el resto de la cadena se envia mediante interupciones utilizando la
 ** función @ref EnviarCaracter en la rutina de servicio.
 ** 
 ** @param[in] cadena Puntero con la cadena de caracteres a enviar.
 ** @return Indica si quedan caracteres para enviar por interrupciones.
 */
bool EnviarTexto(const char * cadena);

/** @brief Envio de un caracter en una interrupcion.
 **
 ** Esta función envia un caracter por el puerto serie durante una rutina de
 ** servicio de interrupcion. La misma continua la transmisión inciada por la
 ** función @ref EnviarTexto.
 ** 
 ** @return Indica si se completó el envio de la cadena.
 */
bool EnviarCaracter(void);

/* === Definiciones de variables internas ================================== */

//! Información para el envio de datos por la uart
cola_t cola;

//! Tarea que espera el evento de transmisión completa
TaskType tarea;

/* === Definiciones de variables externas ================================== */

/* === Definiciones de funciones internas ================================== */

bool EnviarTexto(const char * cadena) {
   bool pendiente = FALSE;

   cola.datos = cadena;
   cola.cantidad = strlen(cadena);
   cola.enviados = 0;

   if (cola.cantidad) {
      Chip_UART_SendByte(USB_UART, cola.datos[cola.enviados]);
      cola.enviados++;

      if (cola.enviados < cola.cantidad) {
         Chip_UART_IntEnable(USB_UART, UART_IER_THREINT);
         pendiente = TRUE;
      }
   }
   return (pendiente);
}

bool EnviarCaracter(void) {
   uint8_t eventos;
   bool completo = FALSE;

   eventos = Chip_UART_ReadLineStatus(USB_UART);

   if (eventos & UART_LSR_THRE) {
      Chip_UART_SendByte(USB_UART, cola.datos[cola.enviados]);
      cola.enviados++;

      if (cola.enviados == cola.cantidad) {
         Chip_UART_IntDisable(USB_UART, UART_IER_THREINT);
         completo = TRUE;
      }
   }
   return (completo);
}

/* === Definiciones de funciones externas ================================== */

/** @brief Tarea de configuración
 **
 ** Esta tarea arranca automaticamente en el modo de aplicacion Normal.
 */
TASK(Configuracion) {

   /* Inicializaciones y configuraciones de dispositivos */
   Init_Leds();
   Init_Switches();
   Init_Uart_Ftdi();

   /* Arranque de la alarma para la activación periorica de la tarea Baliza */
   SetRelAlarm(RevisarTeclado, 250, 100);

   /* Terminación de la tarea */
   TerminateTask();
}

/** @brief Tarea que escanea el teclado
 **
 ** Esta tarea se activa cada vez que expira la alarma RevisarTeclado, lee
 ** el estado actual de la teclas y lo compara con el de la ultima activacion
 ** para detectar los cambios en las teclas y generar eventos al detectar la
 ** pulsación de una tecla.
 */
TASK(Teclado) {
   static uint8_t anterior = 0;
   uint8_t tecla;

   tecla = Read_Switches();
   if (tecla != anterior) {
      switch(tecla) {
      case TEC1:
         ActivateTask(Enviar);
         break;
      case TEC2:
         break;
      case TEC3:
         break;
      case TEC4:
         SetEvent(Enviar, Completo);
         break;
      }
      anterior = tecla;
   }
   Led_Toggle(GREEN_LED);

   /* Terminación de la tarea */
   TerminateTask();
}

/** @brief Tarea que envia la cadena
 **
 ** Esta tarea se activa cada vez que presiona la tecla uno y transmite la
 ** cadena Hola y despues la cadena Mundo
 */
TASK(Enviar) {
   
   Led_On(YELLOW_LED);
   /* Espera que se transmita la primera cadena */
   if (EnviarTexto("Estan ahí mis vidaas? ")) {
      GetTaskID(&tarea);
      ClearEvent(Completo);
      WaitEvent(Completo);
   }
   /* Espera que se transmita la segunda cadena */
   if (EnviarTexto("Me oyen? Me escuchan? Me sienten?\r\n")) {
      GetTaskID(&tarea);
      ClearEvent(Completo);
      WaitEvent(Completo);
   }
   Led_Off(YELLOW_LED);

   /* Terminación de la tarea */
   TerminateTask();
}

/** @brief Rutina de servicio interrupcion serial
 **
 ** Esta rutina se activa cada vez que se vacia el buffer de transmisión de
 ** la uart y se encarga de enviar el siguiente caracter y si se completó la
 ** transmisión entonces notifica a la tarea con un evento.
 */
ISR(EventoSerial) {
   if (EnviarCaracter()) {      
      SetEvent(tarea, Completo);
   };
}

/** @brief Tarea que aumenta contador de segundos
 **
 ** Esta tarea se activa cada vez que expira la alarma IncrementarSegundo
 **
 */
TASK(Aumento) {

   Led_On(RGB_B_LED);
   /* Terminación de la tarea */
   TerminateTask();
}

/** @brief Función para interceptar errores
 **
 ** Esta función es llamada desde el sistema operativo si una función de 
 ** interface (API) devuelve un error. Esta definida para facilitar la
 ** depuración y detiene el sistema operativo llamando a la función
 ** ShutdownOs.
 **
 ** Los valores:
 **    OSErrorGetServiceId
 **    OSErrorGetParam1
 **    OSErrorGetParam2
 **    OSErrorGetParam3
 **    OSErrorGetRet
 ** brindan acceso a la interface que produjo el error, los parametros de 
 ** entrada y el valor de retorno de la misma.
 **
 ** Para mas detalles consultar la especificación de OSEK:
 ** http://portal.osek-vdx.org/files/pdf/specs/os223.pdf
 */
void ErrorHook(void) {
   Led_On(RED_LED);
   ShutdownOS(0);
}

/** @brief Función principal del programa
 **
 ** @returns 0 La función nunca debería terminar
 **
 ** \remark En un sistema embebido la función main() nunca debe terminar.
 **         El valor de retorno 0 es para evitar un error en el compilador.
 */
int main(void) {

   /* Inicio del sistema operatvio en el modo de aplicación Normal */
   StartOS(Normal);

   /* StartOs solo retorna si se detiene el sistema operativo */
   while(1);

   /* El valor de retorno es solo para evitar errores en el compilador*/
   return 0;
}

/* === Ciere de documentacion ============================================== */

/** @} Final de la definición del modulo para doxygen */

