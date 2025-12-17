#include "measure.h"
#include "string.h"

static int16_t v_buf[NUM_SAMPLES_ACCUM];
static int16_t i_buf[NUM_SAMPLES_ACCUM];
static size_t  sample_index = 0;

bool measure_add_sample(int16_t v_mv, int16_t i_mv){

    v_buf[sample_index] = v_mv;
    i_buf[sample_index] = i_mv;
    sample_index++;

    if(sample_index >= NUM_SAMPLES_ACCUM){
        sample_index = 0;
        return true;
    }
    return false;
}

void measure_get_results(measure_t *out){

    double sum_v = 0.0, v_dc, v_ac_meas, v_ac_real, v_pk = 0.0;
    double sum_i = 0.0, i_dc, i_ac_meas, i_ac_real, i_pk = 0.0;
    double sum_rms_v = 0.0, sum_rms_i = 0.0, sum_p_inst = 0.0;
    double Vrms, Irms, P, S, fp;

    for(uint16_t k = 0; k < NUM_SAMPLES_ACCUM; k++){
        sum_v += v_buf[k];
        sum_i += i_buf[k];
    }

    v_dc = sum_v / (double)NUM_SAMPLES_ACCUM;
    i_dc = sum_i / (double)NUM_SAMPLES_ACCUM;

    for(uint16_t k = 0; k < NUM_SAMPLES_ACCUM; k++){
        v_ac_meas = ((double)v_buf[k] - v_dc)/1000.0;
        v_ac_real = v_ac_meas / VOLT_DRIVER_GAIN;
        i_ac_meas = ((double)i_buf[k] - i_dc)/1000.0;
        i_ac_real = i_ac_meas / ACS712_5A_SENSITIVITY;

        if(v_ac_real > v_pk) v_pk = v_ac_real;
        if(i_ac_real > i_pk) i_pk = i_ac_real;

        sum_rms_v += v_ac_real * v_ac_real;
        sum_rms_i += i_ac_real * i_ac_real;
        sum_p_inst += v_ac_real * i_ac_real;
    }

    Vrms = sqrt(sum_rms_v / (double)NUM_SAMPLES_ACCUM);
    Irms = sqrt(sum_rms_i / (double)NUM_SAMPLES_ACCUM);
    P = (sum_p_inst / (double)NUM_SAMPLES_ACCUM);
    if(Vrms <= VOLT_DRIVER_GROUNDNOISE){
        Vrms = 0;
        P = 0;
    }
    if(Irms <= ACS712_GROUNDNOISE){
        Irms = 0;
        P = 0;
    }
    S = Vrms * Irms;
    fp = (S > 1e-6) ? fabs(P) / S : 0.0;

    out->Vrms = Vrms;
    out->VDC = v_dc/1000.0;
    out->Vpk = v_pk;
    out->Irms = (Irms <= ACS712_OFFSET)? 0.0 : (Irms - ACS712_OFFSET);
    out->IDC = i_dc/1000.0;
    out->Ipk = i_pk;
    out->P = P;
    out->S = S;
    out->fp = fp;
    out->E = P*(double)TIME_SAMPLE_H;
}

void measure_display_results(measure_t results){

    printf("\nResultados medición:\n");
    printf(" Tensiones:\n  Vrms = %.2f V,\n  Vdc = %.2f V,\n  Vpk = %.2f V,\n", results.Vrms, results.VDC, results.Vpk);
    printf(" Corrientes:\n  Irms = %.2f A,\n  Idc = %.2f A,\n  Ipk_real = %.2f A,\n", results.Irms, results.IDC, results.Ipk);
    printf(" Potencia:\n  P = %.2f W,\n  S = %.2f VA,\n  fp = %.3f\n", results.P, results.S, results.fp);
}
