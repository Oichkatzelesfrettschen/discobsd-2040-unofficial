/*
 * Discrete PID controller with a trapezoidal integral clamped to its own
 * limits and a band-limited derivative taken on the measurement, so a
 * setpoint step produces no derivative kick.
 */

#include "calc_internal.h"

calc_status
calc_pid_init(calc_pid *c, double kp, double ki, double kd, double tau, double period,
    double out_lo, double out_hi, double integ_lo, double integ_hi)
{
	if (c == NULL)
		return CALC_E_ARG;
	if (!(period >= 0.001) || !(tau >= 0.0) || !(out_lo <= out_hi) ||
	    !(integ_lo <= integ_hi) || !calc__isfinite(period) ||
	    !calc__isfinite(tau) || !calc__isfinite(kp) || !calc__isfinite(ki) ||
	    !calc__isfinite(kd))
		return CALC_E_ARG;
	c->kp = kp;
	c->ki = ki;
	c->kd = kd;
	c->tau = tau;
	c->period = period;
	c->out_lo = out_lo;
	c->out_hi = out_hi;
	c->integ_lo = integ_lo;
	c->integ_hi = integ_hi;
	c->integrator = 0.0;
	c->last_error = 0.0;
	c->differentiator = 0.0;
	c->last_measurement = 0.0;
	c->output = 0.0;
	return CALC_OK;
}

static double
clamp(double v, double lo, double hi)
{
	if (v > hi)
		return hi;
	if (v < lo)
		return lo;
	return v;
}

double
calc_pid_update(calc_pid *c, double setpoint, double measurement)
{
	double e = setpoint - measurement;
	double p = c->kp * e;

	c->integrator += 0.5 * c->ki * c->period * (e + c->last_error);
	c->integrator = clamp(c->integrator, c->integ_lo, c->integ_hi);
	c->differentiator = -(2.0 * c->kd * (measurement - c->last_measurement) +
	    (2.0 * c->tau - c->period) * c->differentiator) /
	    (2.0 * c->tau + c->period);
	c->output = clamp(p + c->integrator + c->differentiator, c->out_lo, c->out_hi);
	c->last_error = e;
	c->last_measurement = measurement;
	return c->output;
}
