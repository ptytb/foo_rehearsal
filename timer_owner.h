#pragma once

#define TIMER_OWNER OWNER_SELF

class TimerOwner
{
public:
	virtual void update_status() = 0;

};
