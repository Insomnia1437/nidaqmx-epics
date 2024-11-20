# NI-DAQmx epics driver

This is an epics driver for NI PXIe module (using NI-DAQmx Linux driver).

This Driver is tested on AlmaLinux 9

NI devices used
- ADC PXIe-6356
- DAC PXI-6733
- PXIe-8381
- Chassis PXIe-1082

[NI Linux driver download](https://www.ni.com/en/support/downloads/drivers/download.ni-daq-mx.html#548387)


[Example code of NI-DAQmx](https://www.ni.com/en/support/documentation/supplemental/18/ni-daqmx-base-example-locations-in-windows--linux-or-mac-os-x.html), You can also find NI-DAQmx example code and header file in this repo.


### NI Linux Driver Installation
```shell
# Latest driver is 2024Q4
$ yum install -y ni-rhel9-drivers-2023Q2.rpm
$ yum repolist
repo id                                                                                            repo name
appstream                                                                                          AlmaLinux 9 - AppStream
baseos                                                                                             AlmaLinux 9 - BaseOS
extras                                                                                             AlmaLinux 9 - Extras
ni-software-2023                                                                                   NI Linux Software 2023 Q2

$ yum install -y ni-daqmx ni-daqmx-labview-support

# (Recommended from NI, but may fail due to old GLIBC version on Old Linux)
#$ yum install ni-hwcfg-utility

$ dkms autoinstall
$ nilsdev
```

### Driver Usage

To use this driver, add below rules to `**App/src/Makefile`

```Makefile
USR_LDFLAGS += -L/usr/lib/x86_64-linux-gnu
**_DBD += pmNiDaq.dbd
SYS_PROD_LIBS += nidaqmx
SYS_PROD_LIBS += m
```

Example epics record

```
record(waveform, "$(P):$(HOST):ADC:WF")
{
  field(DTYP, "NIDAQmxADC")
  field(SCAN, "I/O Intr")
  # Total ADC WF Size
  field(NELM, "$(TAWS)")
  field(FTVL, "DOUBLE")
  field(FLNK, "$(P):$(HOST):Start:Main")
}
record(waveform, "$(P):$(HOST):DAC:WF")
{
  field(DTYP, "NIDAQmxDAC")
  # Total DAC WF Size
  field(NELM, "$(TDWS)")
  field(FTVL, "DOUBLE")
}
```

In `st.cmd`
```shell
pmConfigNiDaqDAC("${DAC_CH}","${DAC_CLK_SRC}","${DAC_TRIG}","${DAC_CNT_SRC}","${DAC_SPC}","${DAC_RATE}")

pmConfigNiDaqADC("${ADC_CH}","${ADC_CLK_SRC}","${ADC_TRIG}","${ADC_CNT_SRC}","${ADC_SPC}","${ADC_RATE}","${ADC_BUF}", "${ADC_FILTER}")

```