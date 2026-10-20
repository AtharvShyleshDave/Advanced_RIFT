# RIFT: RF Intelligent Topology Tracker

An embedded RF sensing and intelligent communication system using ESP32 and nRF24L01 transceivers.

## Overview

RIFT (RF Intelligent Topology Tracker) is a four-node RF sensing system designed to characterize wireless propagation across multiple spatial links and frequency channels. The system uses an ESP32-controlled nRF24L01 network arranged in a tetrahedral topology to capture packet-level RF behavior.

Machine-learning models process statistical, frequency-dependent, and spatial RF features to identify an unknown material among five predefined classes:

- Concrete
- Glass
- Metal
- Plastic
- Wood

After material identification, RIFT applies a learned material-conditioned channel-selection policy to determine a suitable communication channel.

## Objectives

1. Identify an unknown material from its RF propagation signature.
2. Characterize RF behavior across multiple links and frequency channels.
3. Develop leakage-safe machine-learning representations.
4. Use material information to select communication channels adaptively.
5. Demonstrate an embedded TinyML deployment pathway on ESP32.

## Hardware Architecture

RIFT uses four RF nodes:

| Node | Controller | Role |
|------|------------|------|
| A | ESP32 #1 | RF node |
| B | ESP32 #2 | RF node |
| C | ESP32 #1 | RF node |
| D | ESP32 #2 | RF node |

The four nodes form a tetrahedral topology with six possible links:

`AB, AC, AD, BC, BD, CD`

### RF Configuration

- MCU: ESP32
- Transceiver: nRF24L01
- Data rate: 250 kbps
- RF power: PA_MAX
- Acknowledgement: AutoAck
- Retries: 15
- CRC: 16-bit
- SPI:
  - SCK: GPIO 18
  - MISO: GPIO 19
  - MOSI: GPIO 23
- Channel frequency:

`f_c = 2400 + n MHz`

## RF Measurements

The primary communication metric is Packet Delivery Ratio (PDR):

`PDR = N_ACK / N_TX`

The system records link identity, channel identity, packet outcomes, and radio metadata.

RPD measurements are treated as threshold indicators rather than calibrated RSSI, phase, or time-of-flight measurements.

## Machine-Learning Pipeline

RIFT uses a staged representation-development pipeline:

### Stage 1: Packet / Statistical Representation

Establishes a baseline using packet-level and statistical RF measurements.

### Stage 2: Frequency Representation

Investigates channel-dependent RF signatures.

### Stage 2B: Controlled Multi-Frequency Representation

Evaluates controlled frequency-response features across multiple channels.

### Stage 3: Spatial / Multi-Link Representation

Aggregates RF measurements across the tetrahedral topology to capture spatial differences between links.

### Stage 4: Feature Fusion

Combines complementary statistical, frequency, and spatial representations.

### Stage 5: Robustness Analysis

Evaluates representation stability and robustness.

### Stage 6: Material Identification + Channel Adaptation

The final stage combines two objectives:

1. Predict the material class.
2. Map the predicted material to a communication-channel policy.

## Material Classes

RIFT currently evaluates five material classes:

| Class |
|-------|
| Concrete |
| Glass |
| Metal |
| Plastic |
| Wood |

## Stage 6 Results

Current final evaluation:

| Metric | Result |
|--------|--------|
| Material Accuracy | 52.26% |
| Macro-F1 | 52.09% |
| Fixed Mean PDR | 0.8148 |
| Adaptive Mean PDR | 0.8086 |
| Oracle Mean PDR | 0.8212 |
| Material → Channel Agreement | 56.41% |

The current adaptive policy does not yet outperform the fixed-channel baseline. The project therefore treats the present implementation as a proof of concept rather than claiming demonstrated communication improvement.

## Learned Channel Policy

The development/training data produced the following primary channel policy:

| Material | Primary Channel | Mean PDR |
|----------|-----------------|----------|
| Concrete | 1 | 0.796025 |
| Glass | 11 | 0.793551 |
| Metal | 11 | 0.792223 |
| Plastic | 1 | 0.792388 |
| Wood | 6 | 0.792338 |

The policy is learned using development/training data and is not selected using final-test observations.

## Embedded Deployment

The intended runtime pipeline is:

```text
RF MEASUREMENT
      ↓
FEATURE CONSTRUCTION
      ↓
TINYML MATERIAL PREDICTION
      ↓
MATERIAL-CONDITIONED CHANNEL POLICY
      ↓
nRF24L01 CHANNEL CONTROL
      ↓
RF RE-MEASUREMENT
