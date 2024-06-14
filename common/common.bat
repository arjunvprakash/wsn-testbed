for /D %%i in (../*) do (
	if "%%i" NEQ "common" (
		copy /Y "GPIO\\GPIO.h" "..\\%%i\\GPIO\\GPIO.h"
		copy /Y "GPIO\\GPIO.c" "..\\%%i\\GPIO\\GPIO.c"

		copy /Y "I2C\\I2C.h" "..\\%%i\\I2C\\I2C.h"
		copy /Y "I2C\\I2C.c" "..\\%%i\\I2C\\I2C.c"

		copy /Y "SX1262\\SX1262.h" "..\\%%i\\SX1262\\SX1262.h"
		copy /Y "SX1262\\SX1262.c" "..\\%%i\\SX1262\\SX1262.c"

		copy /Y "ALOHA\\ALOHA.h" "..\\%%i\\ALOHA\\ALOHA.h"
		copy /Y "ALOHA\\ALOHA.c" "..\\%%i\\ALOHA\\ALOHA.c"

		copy /Y "MACAW\\MACAW.h" "..\\%%i\\MACAW\\MACAW.h"
		copy /Y "MACAW\\MACAW.c" "..\\%%i\\MACAW\\MACAW.c"

		copy /Y "STEM\\STEM.h" "..\\%%i\\STEM\\STEM.h"
		copy /Y "STEM\\STEM.c" "..\\%%i\\STEM\\STEM.c"

		copy /Y "Routing\\Routing.h" "..\\%%i\\Routing\\Routing.h"
		copy /Y "Routing\\Routing.c" "..\\%%i\\Routing\\Routing.c"

		copy /Y "common.h" "..\\%%i\\common.h"
	)
)

pause
