# QuailTracker Feature Roadmap

Features identified from AudioMoth analysis and project requirements.

## High Priority

### 1. Amplitude Trigger
- [ ] Define amplitude threshold data structure
- [ ] Add threshold configuration to `config.h`
- [ ] Implement RMS/peak level calculation in audio capture
- [ ] Add trigger state machine (idle → triggered → recording → cooldown)
- [ ] Pre-trigger buffer (capture audio just before threshold crossed)
- [ ] Post-trigger hold time (continue recording after level drops)
- [ ] Serial menu option to configure/test threshold
- [ ] Test with actual bobwhite calls

**Impact:** Reduce storage 80%+, extend battery life significantly

### 2. GUANO Metadata
- [ ] Research GUANO specification format
- [ ] Create GUANO chunk builder function
- [ ] Add metadata fields:
  - [ ] Device ID / Station ID
  - [ ] Firmware version
  - [ ] GPS coordinates (lat/lon/alt)
  - [ ] GPS timestamp (UTC)
  - [ ] Temperature / Humidity
  - [ ] Battery voltage
  - [ ] Gain setting
  - [ ] Sample rate / bit depth
- [ ] Append GUANO chunk to WAV files after recording
- [ ] Verify metadata readable by standard tools

**Impact:** Enable automated post-processing, correlate recordings with conditions

### 3. Daily Folders
- [ ] Modify `sdWriterStartRecording()` to create date folders
- [ ] Folder format: `/YYYYMMDD/`
- [ ] File format: `HHMMSS_<station_id>.wav`
- [ ] Handle folder creation errors gracefully
- [ ] Test with multi-day recording sessions

**Impact:** Better organization for large deployments, easier data management

### 4. Configurable Gain
- [ ] Document ES7243E gain register settings
- [ ] Add gain level enum to `config.h`
- [ ] Implement `audioSetGain(level)` function
- [ ] Add gain setting to serial menu
- [ ] Save gain preference to NVS/SPIFFS
- [ ] Include gain in GUANO metadata

**Impact:** Optimize for different deployment distances and noise floors

---

## Medium Priority

### 5. Band-Pass Frequency Trigger
- [ ] Research bobwhite whistle frequency characteristics (1.8-2.2 kHz)
- [ ] Implement Goertzel algorithm for target frequency detection
- [ ] Add frequency threshold configuration
- [ ] Combine with amplitude trigger (AND/OR logic)
- [ ] Test false positive rate with environmental sounds
- [ ] Tune parameters with field recordings

**Impact:** Further reduce false triggers from non-target sounds

### 6. Adaptive Wake Timing
- [ ] Measure actual boot-to-ready time
- [ ] Store measured prep time in RTC memory
- [ ] Implement bounds (750ms - 30000ms)
- [ ] Adjust wake time calculation for next session
- [ ] Log prep time statistics

**Impact:** Optimize power consumption, reduce missed recording windows

### 7. High-Pass Filter Configuration
- [ ] Document ES7243E HPF register settings
- [ ] Add HPF cutoff options (8 Hz, 48 Hz, disabled)
- [ ] Implement `audioSetHighPass(freq)` function
- [ ] Add to serial configuration menu
- [ ] Save preference to NVS/SPIFFS

**Impact:** Reduce wind rumble and handling noise in recordings

---

## Future Considerations

### BLE Configuration App
- [ ] Station ID assignment
- [ ] Recording schedule (sunrise/sunset offsets)
- [ ] Gain and filter settings
- [ ] Trigger thresholds
- [ ] Status monitoring
- [ ] Firmware OTA updates

### Sunrise/Sunset Scheduling
- [ ] Implement NOAA solar calculator
- [ ] Store location coordinates
- [ ] Configure pre-sunrise / post-sunset offsets
- [ ] Deep sleep between recording windows

### Multi-Station Sync Validation
- [ ] PPS accuracy measurement
- [ ] Cross-station time drift analysis
- [ ] TDOA calculation verification

---

## Completed Features

- [x] RTOS dual-core architecture
- [x] ES7243E I2S audio capture
- [x] Ring buffer audio pipeline
- [x] SD card WAV recording
- [x] L76K GPS with PPS sync
- [x] Battery voltage monitoring
- [x] SHT30 temperature/humidity sensor
- [x] Serial console menu

---

## Notes

- AudioMoth uses 8 DMA buffers with 1024 sample max transfers
- AudioMoth compression buffer is 512 bytes (matches SD sector size)
- Consider dual-buffer ping-pong DMA for guaranteed continuous capture
- ES7243E datasheet has detailed register map for gain/filter control
