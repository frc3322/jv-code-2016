#include "WPILib.h"
#include "AHRS.h"

const double ticksPerSecond = 44.0;
const double forwardDriveRough = .35;
const double forwardDriveLow = .45;
const double afterTurnForwardDriveRough = 0.2;
const double afterTurnForwardDriveLow = 0.4;
double turnAngle = 105.0;
const double liftPotForward = 4.66;
const double liftPotVertical = 2.85;
const double liftPotBack = 1.83;
const double liftPot45 = (liftPotVertical - liftPotForward) / 2.0 + liftPotForward;

// 0 == rough terrain
// 1 == low bar
const int autonDefense = 0;

const bool autonStraightOnly = false;

enum LiftCommandEnum {MANUAL_FORWARD, MANUAL_BACK, AUTO_FORWARD, AUTO_45, AUTO_VERTICAL, AUTO_BACK, AUTO_GRAVITY, DISABLED};

class Robot: public IterativeRobot
{
	LiftCommandEnum liftCommand;
	AnalogInput liftPot;
	double liftPotAverage;
	double liftAngle;
	double liftErrorSum;
	double liftErrorLast;
	double liftPower;
	AnalogInput ultrasonic;
	Encoder driveEncoder;
	std::shared_ptr<NetworkTable> table;
	Talon shooter;
	Talon lift1;
	Talon lift2;
	RobotDrive myRobot;
	Joystick driveStick;
	Joystick techStick;
    AHRS *ahrs;
	LiveWindow *lw;
	bool driveFast;
	int autonState;
	int timer;
	bool shooterDefaultOn;
	bool lastShooterToggleButton;
	bool driveToggleButton;

public:
	Robot() :
		liftCommand(AUTO_GRAVITY),
		liftPot(0),
		liftPotAverage(0),
		liftAngle(0),
		liftErrorSum(0),
		liftErrorLast(0),
		ultrasonic(1),
		driveEncoder(0, 1, false, Encoder::k4X),
        table(NULL),
		shooter(5),
		lift1(4),
		lift2(6),
		myRobot(2,3,0,1),
		driveStick(1),
		techStick(0),
        ahrs(NULL),
		lw(LiveWindow::GetInstance()),
		driveFast(false),
		autonState(0),
		timer(0),
		shooterDefaultOn(true),
		lastShooterToggleButton(false),
		driveToggleButton(false)
	{
		myRobot.SetExpiration(0.1);
		// myRobot.SetMaxOutput(driveFast ? 0.6 : 0.3);
		//myRobot.SetInvertedMotor(RobotDrive::kFrontLeftMotor, true);
		//myRobot.SetInvertedMotor(RobotDrive::kRearLeftMotor, true);
		//myRobot.SetInvertedMotor(RobotDrive::kFrontRightMotor, true);
		//myRobot.SetInvertedMotor(RobotDrive::kRearRightMotor, true);
		driveEncoder.SetReverseDirection(true);
		driveEncoder.SetSamplesToAverage(5);
		// driveEncoder.SetDistancePerPulse(1.0 / 360.0 * 2.0 * 3.1415 * 1.5);
		driveEncoder.SetDistancePerPulse(1.0 / 360.0);
		driveEncoder.SetMinRate(1.0);
	}
private:
	void RobotInit()
	{
		table = NetworkTable::GetTable("datatable");
		lw = LiveWindow::GetInstance();
		try {
			/* Communicate w/navX MXP via the MXP SPI Bus.                                       */
			/* Alternatively:  I2C::Port::kMXP, SerialPort::Port::kMXP or SerialPort::Port::kUSB */
			/* See http:b//navx-mxp.kauailabs.com/guidance/selecting-an-interface/ for details.   */
			ahrs = new AHRS(SPI::Port::kMXP);
		} catch (std::exception ex) {
			std::string err_string = "Error instantiating navX MXP:  ";
			err_string += ex.what();
			DriverStation::ReportError(err_string.c_str());
		}
		if ( ahrs ) {
			LiveWindow::GetInstance()->AddSensor("IMU", "Gyro", ahrs);
		}
		CameraServer::GetInstance()->SetQuality(50);
		//the camera name (ex "cam0") can be found through the roborio web interface
		CameraServer::GetInstance()->StartAutomaticCapture("cam0");
	}
	void AutonomousInit()
	{
		autonState = 0;
		timer = 0;
		ahrs->Reset();
		driveEncoder.Reset();
		shooter.Set(0.25, 0);

		if (autonDefense == 0) {			// rough terrain
			// Rough
			liftCommand = AUTO_VERTICAL;
		} else if (autonDefense == 1) {	// low bar
			turnAngle = -turnAngle;
			liftCommand = AUTO_45;
		} else {					// go straight
			liftCommand = AUTO_45;
		}

	}
	void AutonomousPeriodic()

	{
		SmartDash();
		timer++;
		LiftControl();

		if (autonDefense == 0) {		// Rough Terrain
			// wait for lift arm to reach position
			if (autonState == 0) {
				if (timer > 0 * ticksPerSecond) {
					autonState++;
					timer = 0;
				}
			}

			// drive backward to wall
			if (autonState == 1) {
				DriveGyro(-0.6, 0);
				if (timer > 6 * ticksPerSecond) {
					autonState++;
				}
			}
			// Slow down before hit
			if (autonState == 2) {
				DriveGyro(-0.3, 0);
				if (timer > 7 * ticksPerSecond  && !autonStraightOnly) {
					driveEncoder.Reset();
					autonState++;
				}
			}

			// drive forward off wall
			if (autonState == 3) {
				DriveGyro(0.5, 0);
				if (abs(driveEncoder.GetDistance()) > forwardDriveRough) {
					autonState++;
				}
			}


			// turn clockwise 110 degrees
			// clockwise is (+,-)
			if (autonState == 4) {
				myRobot.SetLeftRightMotorOutputs(.8, -.8);
				if (abs(GetAngle()) > abs(turnAngle)) {
					driveEncoder.Reset();
					autonState++;
				}
			}

			// drive forward after turning
			if (autonState == 5) {
				DriveGyro (0.5, turnAngle);
				liftCommand = AUTO_45;
				if (abs(driveEncoder.GetDistance()) > afterTurnForwardDriveRough) {
					timer = 0;
					autonState++;
				}
			}

			// lower arm
			if (autonState == 6) {
				liftCommand = AUTO_FORWARD;
				DriveGyro (0, turnAngle);
				if (timer > 1 * ticksPerSecond) {
					autonState++;
				}
			}

			// drive forward and shoot
			if (autonState == 7) {
				DriveGyro (0.5, turnAngle);
				shooter.Set (1,0);
			}

		} else if (autonDefense == 1) {		// low bar

			// wait for lift arm to reach position
			if (autonState == 0) {
				if (timer > 1 * ticksPerSecond) {
					autonState++;
					timer = 0;
				}
			}

			// drive backward to wall
			if (autonState == 1) {
				DriveGyro(-0.6, 0);
				if (timer > 7 * ticksPerSecond && !autonStraightOnly) {
					driveEncoder.Reset();
					autonState++;
				}
			}

			// drive forward off wall
			if (autonState == 2) {
				DriveGyro(0.5, 0);
				if (abs(driveEncoder.GetDistance()) > forwardDriveLow) {
					autonState++;
				}
			}


			// turn counter clockwise 110 degrees
			// clockwise is (+,-)
			if (autonState == 3) {
				myRobot.SetLeftRightMotorOutputs(-.8, .8);
				if (abs(GetAngle()) > abs(turnAngle)) {
					driveEncoder.Reset();
					autonState++;
				}
			}

			// drive forward after turning
			if (autonState == 4) {
				DriveGyro (0.4, turnAngle);
				if (abs(driveEncoder.GetDistance()) > afterTurnForwardDriveLow) {
					timer = 0;
					autonState++;
				}
			}

			// lower arm
			if (autonState == 5) {
				liftCommand = AUTO_FORWARD;
				DriveGyro (0, turnAngle);
				if (timer > 1 * ticksPerSecond) {
					autonState++;
				}
			}

			// drive forward and shoot
			if (autonState == 6) {
				DriveGyro (0.4, turnAngle);
				shooter.Set (1,0);
			}
		}
	}

	void TeleopInit()
	{
		timer = 0;
		shooterDefaultOn=true;
		lift1.Disable();
		lift2.Disable();
		shooter.Disable();
		liftCommand = AUTO_GRAVITY;
		liftErrorSum = 0;
		liftErrorLast = 0;
		ahrs->Reset();

	}

	void TeleopPeriodic()
	{
		SmartDash();

		// Control the lift arm

		// The manual lift buttons have no lasting effect.  Once you release a manual
		// lift button, the lift arm is set to gravity compensation.  In contrast,
		// the auto and disable lift buttons cause a lasting effect until you press another lift button.
		if (liftCommand == MANUAL_FORWARD || liftCommand == MANUAL_BACK) {
			liftCommand = AUTO_GRAVITY;
		}

		if (techStick.GetRawButton(6)) { // RB
			liftCommand = MANUAL_FORWARD;
		} else if (techStick.GetRawButton(5)) { // LB
			liftCommand = MANUAL_BACK;
		} else if (techStick.GetRawButton(2)) { // B
			liftCommand = AUTO_FORWARD;
		} else if (techStick.GetRawButton(4)) { // Y
			liftCommand = AUTO_45;
		} else if (techStick.GetRawButton(3)) { // X
			liftCommand = AUTO_BACK;
		} else if (techStick.GetRawButton(10)) { // push down right stick
			liftCommand = DISABLED;
		}
		LiftControl();

		// Control the shooter
		if (techStick.GetRawAxis(2)) {
			shooter.Set(1, 0);
		} else if (techStick.GetRawAxis(3)) {
			shooter.Set(-1, 0);
		} else if (shooterDefaultOn) {
			shooter.Set(0.2, 0);
		} else {
			shooter.Disable();
		}

		// Control the default state of the shooter
		if (!lastShooterToggleButton && techStick.GetRawButton(8)) { // START button
			shooterDefaultOn = !shooterDefaultOn;
		}
		lastShooterToggleButton = techStick.GetRawButton(8);

		// Control the drive speed
		if (!driveToggleButton && driveStick.GetRawButton(1)) { // A
			driveFast = !driveFast;
			// myRobot.SetMaxOutput(driveFast ? 0.6 : 0.3);
		}
		driveToggleButton = driveStick.GetRawButton(1);

		// Compute power for the drive motors (do this manually so we can adjust for different strength motors)
		double basePower = (driveFast ? 1 : 0.75) * (-driveStick.GetRawAxis(1));
		double turn = (driveFast ? 0.9 : 0.9) * driveStick.GetRawAxis(4);
		double leftPower = (basePower + turn); // left motor is stronger?
		leftPower *= (basePower > 0) ? 0.9 : 1.03;
		double rightPower = basePower - turn;
		myRobot.SetLeftRightMotorOutputs(leftPower, rightPower);

		//myRobot.ArcadeDrive(driveStick.GetY(), driveStick.GetX());
		//myRobot.TankDrive(driveStick.GetRawAxis(1),driveStick.GetRawAxis(5));
		//myRobot.ArcadeDrive(driveStick); // drive with arcade style (use right stick)

	}
	void TestPeriodic()
	{
		lw->Run();
	}

	void DriveGyro(double outputMagnitude, double angle)
	{
		float angleError = GetAngle() - angle;
		angleError = angleError*.05;
		myRobot.Drive(outputMagnitude, angleError);
	}

	// control the lift arm motor
	void LiftControl()
	{
		// compute average of several liftPot samples
		double liftPotTotal;
		int liftPotCount;
		for (liftPotCount = 0; liftPotCount < 5; liftPotCount++) {
			liftPotTotal += liftPot.GetVoltage();
		}
		liftPotAverage = liftPotTotal / liftPotCount;

		// compute power needed to compensate for gravity (negative power moves from forward->vertical)
		liftAngle = (liftPotAverage-liftPotForward)*90.0/(liftPotVertical-liftPotForward);
		double liftGravity = 0.5 * cos(liftAngle * 3.1416 / 180.0);

		// liftGravity = 0;

		if (liftCommand == DISABLED) {
			liftPower = 0;
		} else if (liftCommand == MANUAL_FORWARD) {
			liftPower = liftGravity - .5;
			// liftPower = -1.0;
		} else if (liftCommand == MANUAL_BACK) {
			liftPower = liftGravity + .5;
			// liftPower = 1.0;
		} else {
			// automatic control

			if (liftCommand == AUTO_GRAVITY) {
				liftPower = liftGravity;
			} else {
				// move to target position (and also compensate for gravity)
				double liftSetPoint;
				if (liftCommand == AUTO_FORWARD) {
					liftSetPoint = liftPotForward;
				} else if (liftCommand == AUTO_45) {
					liftSetPoint = liftPot45;
				} else if (liftCommand == AUTO_VERTICAL) {
					liftSetPoint = liftPotVertical;
				} else if (liftCommand == AUTO_BACK) {
					liftSetPoint = liftPotBack;
				}
				double liftError = liftPotAverage - liftSetPoint;

				// compute P term
				double liftProportional = .3 * liftError;
				// liftProportional = 0;

				// compute I term
				liftErrorSum = .97 * liftErrorSum + liftError;
				double liftIntegral =  .002 * liftErrorSum;

				// liftIntegral = 0;

				// compute D term
				double liftDerivative = 3.0 * (liftError - liftErrorLast);
				liftErrorLast = liftError;

				// liftDerivative = 0;

				liftPower = liftGravity + liftProportional + liftIntegral + liftDerivative;

				if ((liftCommand == AUTO_FORWARD || liftCommand == AUTO_BACK) && abs(liftError) < .5) {
					liftCommand = DISABLED;
				}
			 }
		}
		if (liftCommand == DISABLED) {
			lift1.Disable();
			lift2.Disable();
		} else {
			lift1.Set(-liftPower, 0);
			lift2.Set(liftPower, 0);
		}
	}

	void SmartDash()
	{
		SmartDashboard::PutNumber("IMU Yaw", GetAngle());
		SmartDashboard::PutNumber("Encoder Distance", driveEncoder.GetDistance());
		SmartDashboard::PutNumber("Encoder Rate", driveEncoder.GetRate());
		SmartDashboard::PutNumber("Lift Potentiometer Voltage (average)", liftPotAverage);
		SmartDashboard::PutNumber("liftPotForward", liftPotForward);
		SmartDashboard::PutNumber("liftPot45", liftPot45);
		SmartDashboard::PutNumber("liftPotVertical", liftPotVertical);
		SmartDashboard::PutNumber("liftPotBack", liftPotBack);
		SmartDashboard::PutNumber("Lift Angle", liftAngle);
		SmartDashboard::PutNumber("Lift Power", liftPower);
		SmartDashboard::PutNumber("Ultrasonic", ultrasonic.GetValue());
		SmartDashboard::PutNumber("Speed", driveStick.GetRawAxis(1));
		SmartDashboard::PutNumber("Turn", driveStick.GetRawAxis(4));
	}

	// clockwise is positive
	float GetAngle()
	{
		float angle = ahrs->GetAngle();
		if (angle > 180) {
			angle = angle-360;
		}
		return(angle);
	}
};

START_ROBOT_CLASS(Robot)
