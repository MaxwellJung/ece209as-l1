// my_predictor.h
// This file contains a sample my_predictor class.
// It is a simple 32,768-entry gshare with a history length of 15.
// Note that this predictor doesn't use the whole 32 kilobytes available
// for the CBP-2 contest; it is just an example.

#include "tage.h"

class my_update : public branch_update {
public:
	unsigned int pc, br_flags;
};

class my_predictor : public branch_predictor {
public:
	Tage tage_predictor;

	my_predictor(void) {
		tage_predictor.init();
	}

	branch_update* predict(branch_info & b) {
		// Debug print
		// printf("Predict branch @ PC %x\r\n", b.address);
		bool pred;
		my_update* u;
		u = new my_update();
		u->pc = b.address;
		u->br_flags = b.br_flags;
		u->target_prediction(0);

		if (b.br_flags & BR_CONDITIONAL) {
			pred = tage_predictor.predict(b.address);
			// Debug print
			// printf("pred=%x\r\n", pred);
			u->direction_prediction(pred);
		} else {
			u->direction_prediction(true);
		}
		return u;
	}

	void update(branch_update *u, bool taken, unsigned int target) {
		my_update* mu = (my_update*)u;
		// Debug print
		// printf("Update branch @ PC %x\r\n", mu->pc);
		if (mu->br_flags & BR_CONDITIONAL) {
			tage_predictor.update(mu->pc, taken);
		}
		delete mu;
	}
};
