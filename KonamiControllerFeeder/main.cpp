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

using namespace Platform;
using namespace Windows::Devices;
using namespace Windows::Storage;

String^ targetDeviceIidx = L"IIDX Entry model";
String^ targetDeviceSdvx = L"SDVX Entry Model";
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

concurrency::task<void> connectToController(unsigned long long bluetoothAddress) {
	auto leDevice = co_await Bluetooth::BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress);
	auto servicesResult = co_await leDevice->GetGattServicesForUuidAsync(serviceUUID);
	auto service = servicesResult->Services->GetAt(0);
	auto characteristicsResult = co_await service->GetCharacteristicsForUuidAsync(characteristicUUID);
	auto characteristic = characteristicsResult->Characteristics->GetAt(0);

	auto status = co_await characteristic->WriteClientCharacteristicConfigurationDescriptorAsync(Bluetooth::GenericAttributeProfile::GattClientCharacteristicConfigurationDescriptorValue::Notify);

	characteristic->ValueChanged += ref new Windows::Foundation::TypedEventHandler<Bluetooth::GenericAttributeProfile::GattCharacteristic^, Bluetooth::GenericAttributeProfile::GattValueChangedEventArgs^>(
		[characteristic](Bluetooth::GenericAttributeProfile::GattCharacteristic^ gattCharacteristic, Bluetooth::GenericAttributeProfile::GattValueChangedEventArgs^ eventArgs) {
			auto reader = Streams::DataReader::FromBuffer(eventArgs->CharacteristicValue);
			std::vector<unsigned char> data(reader->UnconsumedBufferLength);

			if (!data.empty()) {
				reader->ReadBytes(Platform::ArrayReference<unsigned char>(&data[0], (unsigned int)data.size()));

				/*
				IIDX:
				Notification packet:
				aa xx bb cc dd aa xx bb cc dd

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

				dd = frame count, unsigned byte

				---

				SDVX:
				Notification packet:
				ll rr bb cc dd ll rr bb cc dd

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

				dd = frame count, unsigned byte
				*/

				for (auto idx = 0; idx < data.size(); idx += 5) {
					JOYSTICK_POSITION iReport = {};
					iReport.bDevice = (BYTE)vjoyDevId;
					iReport.wAxisX = (LONG)std::round(((double)data[idx] / 255.0) * 32768.0); // Turntable, or VOL-L
					iReport.wAxisY = (LONG)std::round(((double)data[idx + 1] / 255.0) * 32768.0); // VOL-R
					iReport.lButtons = data[idx + 2] | (data[idx + 3] << 8);

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

	if (args->Length > 1) {
		// Specify vJoy device ID
		vjoyDevId = _wtoi(args[1]->Data());
	}

	std::wcout << "Using vJoy device ID: " << vjoyDevId << std::endl;

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
			if (eventArgs->Advertisement->LocalName != targetDeviceIidx && eventArgs->Advertisement->LocalName != targetDeviceSdvx) {
				return;
			}

			auto serviceUuids = eventArgs->Advertisement->ServiceUuids;

			unsigned int index = -1;
			if (serviceUuids->IndexOf(serviceUUID, &index)) {
				String^ strAddress = ref new String(formatBluetoothAddress(eventArgs->BluetoothAddress).c_str());
				std::wcout << "Target service found on device: " << strAddress->Data() << " " << eventArgs->Advertisement->LocalName->Data() << std::endl;

				bleAdvertisementWatcher->Stop();

				connectToController(eventArgs->BluetoothAddress);
			}
		});
	bleAdvertisementWatcher->Start();

	for (;;) {
		Sleep(5000);
	}

	RelinquishVJD(vjoyDevId);

	return 0;
}
