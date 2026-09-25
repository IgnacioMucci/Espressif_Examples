Este repositorio contiene el proyecto desarrollado para la placa ESP32, utilizando el framework oficial ESP-IDF mediante contenedores de Docker.

Guía de Configuración ESP-IDF con Docker en Windows
Esta guía detalla los pasos para preparar un entorno de desarrollo para el ESP32 en Windows utilizando Docker, WSL2 y Visual Studio Code. A diferencia del entorno nativo de Linux, aquí usaremos PowerShell y WSL2 para lograr los mismos resultados de forma nativa en Windows.
Paso 1: Instalación y Preparación de Docker
Instalar Docker Desktop: Asegúrate de tener instalado Docker Desktop con el backend de WSL2 habilitado.
Descargar la imagen de ESP-IDF: Abre PowerShell y descarga la imagen oficial de Espressif con todas las herramientas de desarrollo:docker pull espressif/idf:release-v5.4

Esta imagen contiene compiladores, librerías y dependencias necesarias para no tener que instalarlas de a una en tu PC.
Paso 2: Conectar el ESP32 por USB y vincular a WSL2
Para que la terminal de Linux (WSL2) pueda leer el puerto USB físico de Windows, se necesita establecer un puente virtual mediante usbipd.

Conecta la placa ESP32 por USB.
Abre PowerShell como Administrador e identifica el puerto USB de la placa:usbipd list
Suponiendo que el dispositivo (por ejemplo, CP2102) está en el BUSID 1-3, vincúlalo (la primera vez requiere bind):usbipd bind --busid 1-3

usbipd attach --busid 1-3 --wsl

Con esto, el puerto /dev/ttyUSB0 aparecerá en la terminal de Ubuntu/WSL2.
Paso 3: Extraer la librería ESP-IDF al entorno local (Windows)
Extraer el código del SDK permite a VS Code habilitar el autocompletado e inspeccionar el código, evitando que se marquen errores de inclusión.
Ejecuta estos comandos en PowerShell:

Crear la carpeta base en el disco C:New-Item -ItemType Directory -Path "C:\esp" -Force
Crear un contenedor temporal:docker run -d --name idf_temp espressif/idf:release-v5.4 tail -f /dev/null
Copiar las librerías a Windows: (Esto copia la carpeta desde /opt/esp/idf en el contenedor hacia C:\esp\idf y puede tardar unos minutos):docker cp idf_temp:/opt/esp/idf C:\esp\idf
Limpiar y eliminar el contenedor:docker stop idf_temp

docker rm idf_temp
Paso 4: Crear tu Proyecto y Configurar VS Code (.vscode)
Puedes copiar un proyecto de ejemplo a tu ruta de proyectos. Abre esta carpeta en VS Code.
Crea una subcarpeta llamada .vscode.
Crea el archivo .vscode/c_cpp_properties.json para mapear las rutas locales de Windows y habilitar el IntelliSense:{

  "configurations": [

    {

      "name": "Win32",

      "includePath": [

        "${workspaceFolder}/**",

        "C:/esp/idf/examples/**",

        "C:/esp/idf/components/**",

        "C:/esp/idf/components/freertos/FreeRTOS-Kernel/**",

        "C:/esp/idf/components/newlib/platform_include/**",

        "C:/esp/idf/components/newlib/platform_include/sys/*"

      ],

      "defines": [],

      "cStandard": "c17",

      "cppStandard": "c++17",

      "intelliSenseMode": "windows-msvc-x64"

    }

  ],

  "version": 4

}

Crea el archivo .vscode/settings.json:{

  "files.associations": {

    "cstdlib": "c",

    "esp_wifi.h": "c"

  }

}
Paso 5: Compilar y Flashear desde el Contenedor
Una vez escrito el código, la compilación y grabación se ejecutan usando la imagen de Docker a través de la terminal de Ubuntu (WSL).

Abre tu terminal de Ubuntu / WSL.
Ingresa a la carpeta del proyecto en la partición montada de Windows.
Ejecuta el contenedor interactivo, montando el proyecto actual y conectando el dispositivo USB:docker run --rm -v $PWD:/project -w /project --device /dev/ttyUSB0:/dev/ttyUSB0 -it espressif/idf:release-v5.4
Una vez dentro de la consola interactiva del contenedor, ejecuta los comandos de ESP-IDF:
      idf.py set-target esp32

idf.py menuconfig

idf.py build

idf.py -p /dev/ttyUSB0 flash monitor

El último comando compila los cambios, sube el código a la memoria Flash del ESP32 y abre la consola serie (monitor) para visualizar la salida.
