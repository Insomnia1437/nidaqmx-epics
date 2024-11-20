#include <NIDAQmx.h>

#include <shareLib.h>
#include <epicsTypes.h>
#include <errlog.h>
#include <alarm.h>
#include <dbDefs.h>
#include <dbCommon.h>
#include <dbAccess.h>
#include <dbEvent.h>
#include <recGbl.h>
#include <devSup.h>
#include <waveformRecord.h>
#include <epicsExport.h>
#include <cantProceed.h>
#include <iocsh.h>
#include <dbScan.h>
#include "pmFilter.h"
/* Create the dset for devWfRead */
static long init_record_adc(waveformRecord *prec);
static long init_record_dac(waveformRecord *prec);
static long read_wf(waveformRecord *prec);
static long write_wf(waveformRecord *prec);
static long get_ioint_info(int cmd, struct dbCommon *precord, IOSCANPVT *ppvt);
int32 CVICALLBACK pmNiDaqallback(TaskHandle taskHandle, int32 everyNsamplesEventType, uInt32 nSamples, void *callbackData);
static IOSCANPVT ioscanpvt;
static BWLowPass* ADCFilter;
static epicsUInt32 use_filter;

struct
{
    long number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN read_wf;
} devWfRead = {
    5,
    NULL,
    NULL,
    init_record_adc,
    get_ioint_info,
    read_wf},
  devWfWrite = {
    5, 
    NULL, 
    NULL, 
    init_record_dac, 
    NULL, 
    write_wf};
epicsExportAddress(dset, devWfRead);
epicsExportAddress(dset, devWfWrite);

static TaskHandle AItaskHandle = 0, AOtaskHandle = 0, COtaskHandle = 0;
static char errBuff[2048] = {'\0'};
static epicsInt32 dac_samp_per_chan = 0, adc_samp_per_chan = 0;
// NIDAQmx example code uses goto statement to check error
#define DAQmxErrChk(functionCall)            \
    if (DAQmxFailed(error = (functionCall))) \
        goto Error;                          \
    else

int32 CVICALLBACK pmNiDaqallback(TaskHandle taskHandle, int32 everyNsamplesEventType, uInt32 nSamples, void *callbackData)
{
    if(nSamples != adc_samp_per_chan) return 0;
    waveformRecord *prec = (waveformRecord *)callbackData;
    if (DAQmxReadAnalogF64(AItaskHandle, adc_samp_per_chan, 1.0, DAQmx_Val_GroupByChannel, prec->bptr, prec->nelm, (int32*)&prec->nord, NULL) != 0)
    {
        DAQmxGetExtendedErrorInfo(errBuff, 2048);
        (void)errlogPrintf("pmNiDaq: Failed to read the ADC. \n%s\n", errBuff);
        return -1;
    }
    if (use_filter)
    {
        int i = 0;
        for (i = 0; i < prec->nelm; i++)
        {
            ((FTR_PRECISION*)(prec->bptr))[i] = bw_low_pass(ADCFilter, ((FTR_PRECISION*)(prec->bptr))[i]);
        }
    }
    scanIoRequest(prec->dpvt);
    return 0;
}

// ADC device support
static long init_record_adc(waveformRecord *prec)
{
    /* INP must be CONSTANT, PV_LINK, DB_LINK or CA_LINK*/
    switch (prec->inp.type)
    {
    case CONSTANT:
        prec->nord = 0;
        break;
    case PV_LINK:
    case DB_LINK:
    case CA_LINK:
        break;
    default:
        recGblRecordError(S_db_badField, (void *)prec,
                          "pmNiDaq (init_record_adc) Illegal INP field");
        return (S_db_badField);
    }
    if (!AItaskHandle || !COtaskHandle)
    {
        // check whether nidaqmx tasks are created or not
        recGblRecordError(S_dev_NoInit, (void *)prec,
                          "pmNiDaq (init_record_adc) NIDAQmx tasks are not created");
        return (S_dev_NoInit);
    }
    scanIoInit(&ioscanpvt);
    prec->dpvt = ioscanpvt;
    if(use_filter)
    {
        ADCFilter = create_bw_low_pass_filter(4, 1000000, 10000);
    }
    DAQmxRegisterEveryNSamplesEvent(AItaskHandle, DAQmx_Val_Acquired_Into_Buffer, adc_samp_per_chan, 0, pmNiDaqallback, (void *)prec);
    DAQmxStartTask(AItaskHandle);
    DAQmxStartTask(COtaskHandle);
    return 0;
}

static long read_wf(waveformRecord *prec)
{
    epicsUInt32 nord = prec->nord;
    epicsInt32 nRequest = prec->nelm;
    if (nRequest > 0)
    {
        prec->nord = nRequest;
        if (nord != prec->nord)
            db_post_events(prec, &prec->nord, DBE_VALUE | DBE_LOG);

        if (prec->tsel.type == CONSTANT &&
            prec->tse == epicsTimeEventDeviceTime)
            dbGetTimeStamp(&prec->inp, &prec->time);
    }
    return 0;
}
long get_ioint_info(int cmd, struct dbCommon *precord, IOSCANPVT *ppvt)
{
    *ppvt = ioscanpvt;
    return 0;
}

// DAC device support
static long init_record_dac(waveformRecord *prec)
{
    /* INP must be CONSTANT, PV_LINK, DB_LINK or CA_LINK*/
    switch (prec->inp.type)
    {
    case CONSTANT:
        prec->nord = 0;
        break;
    case PV_LINK:
    case DB_LINK:
    case CA_LINK:
        break;
    default:
        recGblRecordError(S_db_badField, (void *)prec,
                          "pmNiDaq (init_record_dac) Illegal INP field");
        return (S_db_badField);
    }
    if (!AOtaskHandle)
    {
        // check whether nidaqmx tasks are created or not
        recGblRecordError(S_dev_NoInit, (void *)prec,
                          "pmNiDaq (init_record_dac) NIDAQmx tasks are not created");
        return (S_dev_NoInit);
    }
    return 0;
}
static long write_wf(waveformRecord *prec)
{
    epicsUInt32 nord = prec->nord;
    epicsInt32 nRequest = prec->nelm;
    // we must stop the task first before writing to it
    if (DAQmxStopTask(AOtaskHandle) < 0)
    {
        DAQmxGetExtendedErrorInfo(errBuff, 2048);
        (void)errlogPrintf("pmNiDaq: Failed to stop DAC task. \n%s\n", errBuff);
        return -1;
    }
    if (DAQmxWriteAnalogF64(AOtaskHandle, dac_samp_per_chan, 0, 1.0, DAQmx_Val_GroupByChannel, (epicsFloat64 *)prec->bptr, (epicsInt32 *)&nord, NULL) < 0)
    {
        DAQmxGetExtendedErrorInfo(errBuff, 2048);
        (void)errlogPrintf("pmNiDaq: Failed to write to the DAC task. \n%s\n", errBuff);
        return -1;
    }
    if (DAQmxStartTask(AOtaskHandle) < 0)
    {
        DAQmxGetExtendedErrorInfo(errBuff, 2048);
        (void)errlogPrintf("pmNiDaq: Failed to start the DAC task. \n%s\n", errBuff);
        return -1;
    }
    if (nRequest > 0)
    {
        prec->nord = nRequest;
        if (nord != prec->nord)
            db_post_events(prec, &prec->nord, DBE_VALUE | DBE_LOG);

        if (prec->tsel.type == CONSTANT &&
            prec->tse == epicsTimeEventDeviceTime)
            dbGetTimeStamp(&prec->inp, &prec->time);
    }
    return 0;
}

void pmConfigNiDaqADC(const char *p0, const char *p1, const char *p2, const char *p3, const int p4, const int p5, const int p6, const int p7)
{
    epicsInt32 error = 0;
    /**
    char adc_chan_name[] = p0;
    char adc_clock_src_name[] = p1;
    char adc_trig_chan_name[] = p2;
    char adc_ctr_src_name[] = p3;
    epicsInt32 adc_samp_per_chan = p4; //6000
    epicsInt32 adc_samp_rate = p5; // 1M
    epicsInt32 adc_buf_size = p6;  // 40M
    **/
    epicsUInt32 size_readback = 0;
    adc_samp_per_chan = p4;
    use_filter = p7;
    DAQmxErrChk(DAQmxCreateTask("", &AItaskHandle));
    DAQmxErrChk(DAQmxCreateAIVoltageChan(AItaskHandle, p0, "", DAQmx_Val_Cfg_Default, -10.0, 10.0, DAQmx_Val_Volts, NULL));
    DAQmxErrChk(DAQmxCfgSampClkTiming(AItaskHandle, p1, p5, DAQmx_Val_Rising, DAQmx_Val_ContSamps, p4));
    DAQmxErrChk(DAQmxSetBufInputBufSize(AItaskHandle, p6));
    DAQmxErrChk(DAQmxGetBufInputBufSize(AItaskHandle, &size_readback));

    if (size_readback != p6)
    {
        (void)errlogPrintf("pmConfigNiDaqADC: ADC Input Buffer config failed %d\n", size_readback);
    }
    DAQmxErrChk(DAQmxCreateTask("", &COtaskHandle));
    DAQmxErrChk(DAQmxCreateCOPulseChanFreq(COtaskHandle, p3, "", DAQmx_Val_Hz, DAQmx_Val_Low, 0, p5, 0.5));
    DAQmxErrChk(DAQmxCfgImplicitTiming(COtaskHandle, DAQmx_Val_FiniteSamps, p4));
    DAQmxErrChk(DAQmxCfgDigEdgeStartTrig(COtaskHandle, p2, DAQmx_Val_Rising));
    DAQmxErrChk(DAQmxSetStartTrigRetriggerable(COtaskHandle, 1));

    // DAQmxStartTask(AItaskHandle);
    // DAQmxStartTask(COtaskHandle);
    printf("Config ADC OK, samples %d\n", adc_samp_per_chan);
Error:
    if (DAQmxFailed(error))
    {
        DAQmxGetExtendedErrorInfo(errBuff, 2048);
        (void)errlogPrintf("pmConfigNiDaqADC Error. \n%s\n", errBuff);
        if (AItaskHandle)
        {
            DAQmxStopTask(AItaskHandle);
            DAQmxClearTask(AItaskHandle);
            AItaskHandle = 0;
        }
        if (COtaskHandle)
        {
            DAQmxStopTask(COtaskHandle);
            DAQmxClearTask(COtaskHandle);
            COtaskHandle = 0;
        }
    }
}

void pmConfigNiDaqDAC(const char *p0, const char *p1, const char *p2, const char *p3, const int p4, const int p5)
{
    epicsInt32 error = 0;
    /**
    char dac_chan_name[] = p0;
    char dac_clock_src_name[] = p1;
    char dac_trig_chan_name[] = p2;
    char dac_ctr_src_name[] = p3;
    int dac_samp_per_chan = p4;
    int dac_samp_rate = p5;
    */
    dac_samp_per_chan = p4;
    DAQmxErrChk(DAQmxCreateTask("", &AOtaskHandle));
    DAQmxErrChk(DAQmxCreateAOVoltageChan(AOtaskHandle, p0, "", -10.0, 10.0, DAQmx_Val_Volts, NULL));
    DAQmxErrChk(DAQmxCfgSampClkTiming(AOtaskHandle, p1, p5, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, p4));
    DAQmxErrChk(DAQmxCfgDigEdgeStartTrig(AOtaskHandle, p2, DAQmx_Val_Rising));

    // TODO check effect of below line
    // DAQmxErrChk(DAQmxStartTask(AOtaskHandle));
    printf("Config DAC OK, samples %d\n", dac_samp_per_chan);
Error:
    if (DAQmxFailed(error))
    {
        DAQmxGetExtendedErrorInfo(errBuff, 2048);
        (void)errlogPrintf("pmConfigNiDaqDAC Error. \n%s\n", errBuff);
        if (AOtaskHandle)
        {
            DAQmxStopTask(AOtaskHandle);
            DAQmxClearTask(AOtaskHandle);
            AOtaskHandle = 0;
        }
    }
}
static const iocshArg pmConfigNiDaqADCArg0 = {"ch", iocshArgString};
static const iocshArg pmConfigNiDaqADCArg1 = {"clk src", iocshArgString};
static const iocshArg pmConfigNiDaqADCArg2 = {"trig ch", iocshArgString};
static const iocshArg pmConfigNiDaqADCArg3 = {"ctr src", iocshArgString};
static const iocshArg pmConfigNiDaqADCArg4 = {"spc", iocshArgInt};
static const iocshArg pmConfigNiDaqADCArg5 = {"rate", iocshArgInt};
static const iocshArg pmConfigNiDaqADCArg6 = {"buffer", iocshArgInt};
static const iocshArg pmConfigNiDaqADCArg7= {"use filter", iocshArgInt};
static const iocshArg *pmConfigNiDaqADCArgs[] = {&pmConfigNiDaqADCArg0, &pmConfigNiDaqADCArg1, &pmConfigNiDaqADCArg2,
                                                 &pmConfigNiDaqADCArg3, &pmConfigNiDaqADCArg4, &pmConfigNiDaqADCArg5,
                                                 &pmConfigNiDaqADCArg6, &pmConfigNiDaqADCArg7};
static const iocshFuncDef pmConfigNiDaqADCFuncDef = {"pmConfigNiDaqADC", 8, pmConfigNiDaqADCArgs};

/* Wrapper called by iocsh, selects the argument types that pmConfigPath needs */
static void pmConfigNiDaqADCCallFunc(const iocshArgBuf *args)
{
    pmConfigNiDaqADC(args[0].sval, args[1].sval, args[2].sval, args[3].sval, args[4].ival, args[5].ival, args[6].ival, args[7].ival);
}

static const iocshArg pmConfigNiDaqDACArg0 = {"ch", iocshArgString};
static const iocshArg pmConfigNiDaqDACArg1 = {"clk src", iocshArgString};
static const iocshArg pmConfigNiDaqDACArg2 = {"trig ch", iocshArgString};
static const iocshArg pmConfigNiDaqDACArg3 = {"ctr src", iocshArgString};
static const iocshArg pmConfigNiDaqDACArg4 = {"spc", iocshArgInt};
static const iocshArg pmConfigNiDaqDACArg5 = {"rate", iocshArgInt};
static const iocshArg *pmConfigNiDaqDACArgs[] = {&pmConfigNiDaqDACArg0, &pmConfigNiDaqDACArg1, &pmConfigNiDaqDACArg2,
                                                 &pmConfigNiDaqDACArg3, &pmConfigNiDaqDACArg4, &pmConfigNiDaqDACArg5};
static const iocshFuncDef pmConfigNiDaqDACFuncDef = {"pmConfigNiDaqDAC", 6, pmConfigNiDaqDACArgs};

/* Wrapper called by iocsh, selects the argument types that pmConfigPath needs */
static void pmConfigNiDaqDACCallFunc(const iocshArgBuf *args)
{
    pmConfigNiDaqDAC(args[0].sval, args[1].sval, args[2].sval, args[3].sval, args[4].ival, args[5].ival);
}

/* Registration routine, runs at startup */
static void pmConfigNiDaqRegister(void)
{
    iocshRegister(&pmConfigNiDaqADCFuncDef, pmConfigNiDaqADCCallFunc);
    iocshRegister(&pmConfigNiDaqDACFuncDef, pmConfigNiDaqDACCallFunc);
}

epicsExportRegistrar(pmConfigNiDaqRegister);
