# Front Worker / Back Worker Architecture

Korean version: [02_front_back_worker_KOR.md](02_front_back_worker_KOR.md)

This document describes the separate roles of Front Workers and Back Workers in the WAN Accelerator.

## 1. Why the Worker Roles Were Separated

The WAN Accelerator does more than simply forward data. It divides received data into chunks, performs deduplication and compression-related processing, and then forwards the result to the peer node.

If a single worker performs all tasks, CPU utilization can become concentrated and data reception, processing, and transmission can interfere with one another.

Therefore, this work separated workers into Front Workers and Back Workers so that the input path and the processing/output path could be handled independently.

## 2. Overall Processing Flow

```text
Receive data from Client or WAN side
-> Front Worker processes input data
-> split into chunks
-> search for duplicate chunks
-> classify new chunks and reference chunks
-> dispatch work to Back Worker
-> Back Worker performs compression/reconstruction/output-buffer generation
-> transmit toward WAN side or Application
```

## 3. Front Worker Role

The Front Worker handles the first stage of the input path.

Main responsibilities include:

- receiving data from the Client or WAN side
- managing input-stream buffers
- splitting received data into chunks
- calculating chunk hashes
- checking whether chunks are duplicates
- classifying new chunks and reference chunks
- dispatching work to Back Workers

In short, the Front Worker performs the front-end processing that determines what kind of data was received.

## 4. Back Worker Role

The Back Worker converts work received from the Front Worker into a form that can be transmitted.

Main responsibilities include:

- receiving chunk work from Front Workers
- processing new chunks
- reconstructing reference chunks
- compression or decompression
- generating output buffers
- transmitting data to the peer WAN Accelerator or Application

In short, the Back Worker performs the back-end processing that turns classified data into transmittable output.

## 5. MS Worker Flow

MS receives requests from the Client and forwards data to ES.

```text
Receive Client request
-> MS Front Worker
-> chunking and duplicate check
-> MS Back Worker
-> WAN-side F-Stack path
-> transmit to ES
```

On MS, F-Stack-based transmission is important when forwarding Client requests into the WAN path.

## 6. ES Worker Flow

ES receives WAN data from MS and forwards it to the Backend Application.

```text
Receive WAN data from MS
-> ES Front Worker
-> interpret chunk/reference data
-> ES Back Worker
-> reconstruct original stream
-> forward to Backend Application
```

On ES, interpreting chunk/reference information and reconstructing the original data are important processing steps.

## 7. CPU Role Separation

In the experiment, some of the six available CPU cores were assigned separately to the F-Stack loop and worker processing.

A representative worker configuration was:

```text
Front Workers: 2
Back Workers : 3
Total Workers: 5
```

In this design, it is important for the F-Stack network loop and the Front/Back Workers to have distinct CPU roles.

## 8. Summary

The Front Worker performs chunking and duplicate classification on the input side.

The Back Worker performs compression, reconstruction, output-buffer generation, and transmission.

Therefore, the WAN Accelerator should be understood as a pipeline that combines Front Worker input analysis with Back Worker output generation rather than as a simple forwarding application.
