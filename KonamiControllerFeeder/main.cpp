// Based on https://github.com/urish/win-ble-cpp by Uri Shaked

#include <iostream>
#include <iomanip>
#include <pplawait.h>
#include <sstream> 
#include <string>
#include <Windows.Devices.Bluetooth.h>
#include <Windows.Devices.Bluetooth.Advertisement.h>
#include <wrl/event.h>

#include <vjoyinterface.h>

#define AXIS_X 0
#define AXIS_Y (AXIS_X + 1)

#define UPDATE_FRAME_DELTA 1

using namespace Platform;
using namespace Windows::Devices;
using namespace Windows::Storage;

enum DeviceType {
	UNKNOWN,
	IIDX,
	SDVX,
	POPN,
	GITADORA_GUITAR,
};

String^ targetDeviceIidx = L"IIDX Entry model";
String^ targetDeviceSdvx = L"SDVX Entry Model";
String^ targetDevicePopn = L"Pop'n controller";
String^ targetDeviceGitadoraGuitar = L"GITADORA controller"; // TODO: Temporary until real name is known

auto serviceUUID = Bluetooth::BluetoothUuidHelper::FromShortId(0xff00);
auto characteristicUUID = Bluetooth::BluetoothUuidHelper::FromShortId(0xff01);
auto vjoyDevId = 1; // Default to device ID 1, but let the user specify a different device ID later

std::wstring formatBluetoothAddress(unsigned long long bluetoothAddress) {
	std::wostringstream ret;
	ret << std::hex << std::setfill(L'0')
		<< std::setw(2) << ((bluetoothAddress >> (5 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((bluetoothAddress >> (4 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((bluetoothAddress >> (3 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((bluetoothAddress >> (2 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((bluetoothAddress >> (1 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((bluetoothAddress >> (0 * 8)) & 0xff);
	return ret.str();
}

int lastRawValue[2] = { 0 };
int currentValue[2] = { 0 };
int lastValue[2] = { 0 };
bool initAxis[2] = { false };
double axisSensitivity[2] = { 1.0, 1.0 };
int lastUpdateFrame = -1;

bool isDigital = false;

concurrency::task<bool> isValidController(unsigned long long bluetoothAddress) {
	auto leDevice = co_await Bluetooth::BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress);
	auto servicesResult = co_await leDevice->GetGattServicesForUuidAsync(serviceUUID);
	co_return servicesResult->Status == Windows::Devices::Bluetooth::GenericAttributeProfile::GattCommunicationStatus::Success;
}

concurrency::task<void> connectToController(unsigned long long bluetoothAddress, String^ deviceName) {
	auto leDevice = co_await Bluetooth::BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress);
	auto servicesResult = co_await leDevice->GetGattServicesForUuidAsync(serviceUUID);

	auto service = servicesResult->Services->GetAt(0);
	auto characteristicsResult = co_await service->GetCharacteristicsForUuidAsync(characteristicUUID);
	auto characteristic = characteristicsResult->Characteristics->GetAt(0);

	auto status = co_await characteristic->WriteClientCharacteristicConfigurationDescriptorAsync(Bluetooth::GenericAttributeProfile::GattClientCharacteristicConfigurationDescriptorValue::Notify);

	DeviceType deviceType = DeviceType::UNKNOWN;

	if (deviceName == targetDeviceIidx) {
		deviceType = DeviceType::IIDX;
	}
	else if (deviceName == targetDeviceSdvx) {
		deviceType = DeviceType::SDVX;
	}
	else if (deviceName == targetDevicePopn) {
		deviceType = DeviceType::POPN;
	}
	else if (deviceName == targetDeviceGitadoraGuitar) {
		deviceType = DeviceType::GITADORA_GUITAR;
	}

	characteristic->ValueChanged += ref new Windows::Foundation::TypedEventHandler<Bluetooth::GenericAttributeProfile::GattCharacteristic^, Bluetooth::GenericAttributeProfile::GattValueChangedEventArgs^>(
		[characteristic, deviceType](Bluetooth::GenericAttributeProfile::GattCharacteristic^ gattCharacteristic, Bluetooth::GenericAttributeProfile::GattValueChangedEventArgs^ eventArgs) {
			auto reader = Streams::DataReader::FromBuffer(eventArgs->CharacteristicValue);
			std::vector<unsigned char> data(reader->UnconsumedBufferLength);

			if (!data.empty()) {
				reader->ReadBytes(Platform::ArrayReference<unsigned char>(&data[0], (unsigned int)data.size()));

				/*
				IIDX:
				Notification packet (5 bytes):
				aa xx bb cc zz

				aa = unsigned byte, turntable value
				Spinning the turntable counter-clockwise increases the value, clockwise decreases the value

				xx = Always 0?

				bb =
				0x01 Button 1
				0x02 Button 2
				0x04 Button 3
				0x08 Button 4
				0x10 Button 5
				0x20 Button 6
				0x40 Button 7

				cc =
				0x01 E1
				0x02 E2

				zz = frame count, unsigned byte

				---

				SDVX:
				Notification packet (5 bytes):
				ll rr bb cc zz

				ll = unsigned byte, VOL-L knob
				Clockwise increases value, counter-clockwise decreases value

				rr = unsigned byte, VOL-R knob
				Clockwise increases value, counter-clockwise decreases value

				bb =
				0x01 BT-A
				0x02 BT-B
				0x04 BT-C
				0x08 BT-D
				0x10 FX-L
				0x20 FX-R

				cc =
				0x01 Start

				zz = frame count, unsigned byte

				---

				pop'n music:
				Notification packet (6 bytes):
				aa bb cc dd ee zz

				aa =
				0x01 Button 1
				0x02 Button 2
				0x04 Button 3
				0x08 Button 4
				0x10 Button 5
				0x20 Button 6
				0x40 Button 7
				0x80 Button 8

				bb =
				0x01 Button 9
				0x20 Start
				0x04 Select

				cc = X Axis?
				dd = Y Axis?
				ee = Z Axis?

				zz = frame count, unsigned byte

				---

				GITADORA (Guitar):
				Notification packet (6? bytes):
				aa bb cc dd ee zz?

				aa =
				0x01 Button 1?
				0x02 Button 2?
				0x04 Button 3?
				0x08 Button 4?
				0x10 Button 5?
				0x20 Start?
				0x40 Select?

				bb = 00?

				cc = X Axis?
				dd = Y Axis?
				ee = Z Axis?

				zz = frame count, unsigned byte
				*/

				int packetLen = 5; // IIDX and SDVX
				if (deviceType == DeviceType::POPN || deviceType == DeviceType::GITADORA_GUITAR) {
					// pop'n music is a stream of packets with a size of 6 byte per packet
					packetLen = 6;
				}

				for (auto idx = 0; idx < data.size(); idx += packetLen) {
					JOYSTICK_POSITION iReport = {};
					iReport.bDevice = (BYTE)vjoyDevId;

					for (int i = 0; i < packetLen; i++) {
						printf("%02x ", data[idx + i]);
					}
					printf("\n");

					if (deviceType == DeviceType::POPN || deviceType == DeviceType::GITADORA_GUITAR) {
						// TODO: Not sure how to scale these values for Gitadora yet. Unused in pop'n but they exist
						iReport.wAxisX = data[idx + 2];
						iReport.wAxisY = data[idx + 3];
						iReport.wAxisZ = data[idx + 4];
						iReport.lButtons = data[idx] | (data[idx + 1] << 8);
					}
					else {
						// IIDX and SDVX have the same format
						if (isDigital) {
							auto frameOverflow = abs(lastUpdateFrame - data[idx + 4]) > 200;
							if (lastUpdateFrame == -1 || (data[idx + 4] - lastUpdateFrame > UPDATE_FRAME_DELTA) || (frameOverflow && (data[idx + 4] - lastUpdateFrame) < UPDATE_FRAME_DELTA)) {
								for (auto axisIdx = 0; axisIdx <= AXIS_Y - AXIS_X; axisIdx++) {
									if (!initAxis[AXIS_X + axisIdx]) {
										lastRawValue[AXIS_X + axisIdx] = data[idx + axisIdx];
										initAxis[AXIS_X + axisIdx] = true;
									}

									auto newValue = data[idx + axisIdx]; // Turntable, or VOL-L
									auto underflow = abs(lastRawValue[AXIS_X + axisIdx] - newValue) > 200;
									auto nextValue = 0;

									if (underflow) {
										if (lastRawValue[AXIS_X + axisIdx] - newValue < 0) {
											nextValue = 0;
										}
										else if (lastRawValue[AXIS_X + axisIdx] - newValue > 0) {
											nextValue = 32768;
										}
										else {
											nextValue = 32768 / 2;
										}
									}
									else {
										if (lastRawValue[AXIS_X + axisIdx] - newValue > 0) {
											nextValue = 0;
										}
										else if (lastRawValue[AXIS_X + axisIdx] - newValue < 0) {
											nextValue = 32768;
										}
										else {
											nextValue = 32768 / 2;
										}
									}

									lastRawValue[AXIS_X + axisIdx] = newValue;
									currentValue[AXIS_X + axisIdx] = nextValue;
									lastUpdateFrame = data[idx + 4];
								}
							}
						}
						else {
							currentValue[AXIS_X] = (LONG)std::round((((double)data[idx] * axisSensitivity[AXIS_X]) / 255.0) * 32768.0) % 32768; // Turntable, or VOL-L
							currentValue[AXIS_Y] = (LONG)std::round((((double)data[idx + 1] * axisSensitivity[AXIS_Y]) / 255.0) * 32768.0) % 32768; // VOL-R
						}

						iReport.wAxisX = currentValue[AXIS_X];
						iReport.wAxisY = currentValue[AXIS_Y];
						iReport.lButtons = data[idx + 2] | (data[idx + 3] << 8);
					}

					// Send position data to vJoy device
					if (!UpdateVJD(vjoyDevId, &iReport)) {
						printf("Feeding vJoy device number %d failed - try to enable device\n", vjoyDevId);
						AcquireVJD(vjoyDevId);
					}
				}
			}
		}
	);
}

int main(Array<String^>^ args) {
	Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);

	auto argIdx = 1;
	while (argIdx < args->Length) {
		auto arg = args[argIdx++];

		if (arg == "--help") {
			std::wcout << "usage: " << args[0]->Data() << " [--sensitivity-x 1.0] [--sensitivity-y 1.0] [--device-id 1] [--digital] [--help]" << std::endl;
			std::wcout << std::endl;
			std::wcout << "arguments:" << std::endl;
			std::wcout << "\t--device-id (val) - Set the target vJoy device ID" << std::endl;
			std::wcout << "\t--sensitivity-x (val) - Set the sensitivity of the X axis for analog mode" << std::endl;
			std::wcout << "\t--sensitivity-y (val) - Set the sensitivity of the Y axis for analog mode" << std::endl;
			std::wcout << "\t--digital - Turn X and Y axis values into digital instead of analog values. Useful for BMS simulators." << std::endl;
			std::wcout << "\t--help - Display this help message" << std::endl;
			std::wcout << std::endl;
			return 0;
		}
		else if (arg == "--device-id" && argIdx < args->Length) {
			auto param = args[argIdx++];
			vjoyDevId = _wtoi(param->Data());
		}
		else if (arg == "--sensitivity-x" && argIdx < args->Length) {
			auto param = args[argIdx++];
			axisSensitivity[AXIS_X] = _wtof(param->Data());
		}
		else if (arg == "--sensitivity-y" && argIdx < args->Length) {
			auto param = args[argIdx++];
			axisSensitivity[AXIS_Y] = _wtof(param->Data());
		}
		else if (arg == "--digital") {
			isDigital = true;
		}
		else {
			std::wcout << "Unknown argument!" << arg->Data() << std::endl;
		}
	}

	CoInitializeSecurity(
		nullptr,
		-1,
		nullptr,
		nullptr,
		RPC_C_AUTHN_LEVEL_DEFAULT,
		RPC_C_IMP_LEVEL_IDENTIFY,
		NULL,
		EOAC_NONE,
		nullptr
	);

	std::wcout << "Using vJoy device ID: " << vjoyDevId << std::endl;

	if (!vJoyEnabled()) {
		wprintf(L"Function vJoyEnabled Failed - make sure that vJoy is installed and enabled\n");
		return -1;
	}

	VjdStat status = GetVJDStatus(vjoyDevId);

	switch (status) {
	case VJD_STAT_OWN:
		printf("vJoy device %d is already owned by this feeder\n", vjoyDevId);
		break;
	case VJD_STAT_FREE:
		printf("vJoy device %d is free\n", vjoyDevId);
		break;
	case VJD_STAT_BUSY:
		printf("vJoy device %d is already owned by another feeder\nCannot continue\n", vjoyDevId);
		return -3;
	case VJD_STAT_MISS:
		printf("vJoy device %d is not installed or disabled\nCannot continue\n", vjoyDevId);
		return -4;
	default:
		printf("vJoy device %d general error\nCannot continue\n", vjoyDevId);
		return -1;
	};

	// Acquire the vJoy device
	if (!AcquireVJD(vjoyDevId)) {
		printf("Failed to acquire vJoy device number %d.\n", vjoyDevId);
		return -1;
	} else {
		printf("Acquired device number %d - OK\n", vjoyDevId);
	}

	Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ bleAdvertisementWatcher = ref new Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher();
	bleAdvertisementWatcher->ScanningMode = Bluetooth::Advertisement::BluetoothLEScanningMode::Active;
	bleAdvertisementWatcher->Received += ref new Windows::Foundation::TypedEventHandler<Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^, Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^>(
		[bleAdvertisementWatcher](Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ watcher, Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^ eventArgs) {
			if (eventArgs->Advertisement->LocalName != targetDeviceIidx 
				&& eventArgs->Advertisement->LocalName != targetDeviceSdvx
				&& eventArgs->Advertisement->LocalName != targetDevicePopn
				&& eventArgs->Advertisement->LocalName != targetDeviceGitadoraGuitar) {
				std::wcout << "Skipping device: \"" << eventArgs->Advertisement->LocalName->Data() << "\"" << std::endl;
				return;
			}

			String^ strAddress = ref new String(formatBluetoothAddress(eventArgs->BluetoothAddress).c_str());

			auto isValid = isValidController(eventArgs->BluetoothAddress);
			if (isValid.get()) {
				std::wcout << "Target service found on device: " << strAddress->Data() << " " << eventArgs->Advertisement->LocalName->Data() << std::endl;

				bleAdvertisementWatcher->Stop();

				connectToController(eventArgs->BluetoothAddress, eventArgs->Advertisement->LocalName);
			}
			else {
				std::wcout << "Target service NOT found on device: " << strAddress->Data() << " " << eventArgs->Advertisement->LocalName->Data() << std::endl;
			}
		});
	bleAdvertisementWatcher->Start();

	for (;;) {
		Sleep(5000);
	}

	RelinquishVJD(vjoyDevId);

	return 0;
}
