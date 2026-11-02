# Spec: staff states, per-frame update and thoughts

## Purpose
What a staff member does when it is sacked and when it enters or leaves a state, what its per-frame update does (mood
announcements, happiness drift, profession extras), and which feedback text and thought list it produces.

## Vocabulary
- **Profession**: builder, cleaner, lifeguard, mechanic, rep, security guard.
- **Home**: the building the member belongs to (home id). **Job state**: a state number in which the member holds a job.
- **Feedback flags**: a 32-bit mask of reasons for a text. Bit 0x400000 can't-do-job, 0x200000 next-job, 0x800000 unhappy,
  0x1000000 start work, 0x2000000 end work, 0x4000000 pay rise, 0x8000000 pay cut, 0x10000000 sacked, 0x20000000 idle,
  0x40000000 directed, 0x80000000 overworked; the low bits and 0x100000 are meaningful per profession.

## Rules: sacking and state changes
1. Sacking always calls the shared sack step first and then sets the state to 0x4c and enters it
   (`staff_state.builder_sack_frees_the_job_in_the_job_states_only`).
2. A builder sacked in the states 0x61..0x64 or 0x67 first frees its builder slot (its own id); in every other state of 0..0x8f
   nothing is freed. The state is read as unsigned, so -1 frees nothing
   (`staff_state.builder_sack_frees_the_job_in_the_job_states_only`, `staff_state.builder_state_is_read_as_unsigned`).
3. A cleaner sacked in 0x6d..0x73, or in 0x83 when it is able to do the job, reports that it did not clean the object it was
   sent to; in 0x83 with the can't-do-job flag set it does not (`staff_state.cleaner_sack_gives_the_job_back_in_states_6d_to_73_and_83_when_able`).
4. A lifeguard leaves its tower, a mechanic returns its repair job and leaves (told whether a repair was found), and a rep
   releases the guest it was sent to, each after the shared step (`staff_state.lifeguard_mechanic_and_rep_sack_call_their_own_leaving`).
5. A security guard sacked in 0x7d..0x7f, or in 0x83 when able, with an offender, posts the offender a message of type 3 with
   data 12 from itself; with no offender, or in other states, nothing is posted
   (`staff_state.security_sack_releases_the_offender_in_the_offence_states`).
6. Entering state 1 tells the home (a message of type 37 from the member to its home) and runs the entry script; a builder is
   also registered with the construction manager; a lifeguard asks for its home's influence area between message and script and
   keeps the area it had when there is no home (`staff_state.state_one_tells_the_home_and_the_lifeguard_copies_the_influence_area`,
   `staff_state.a_lifeguard_without_a_home_keeps_its_area`).
7. Entering the "go home" state of a profession (builder 0x60, cleaner 0x69, mechanic 0x5c) copies the home id into the target.
   Another profession's state leaves the target alone, and no message is sent for any state but 1
   (`staff_state.home_target_states_copy_the_home_into_the_target`).
8. Leaving a state runs the exit script of the current state (`staff_state.exit_runs_the_script_of_the_current_state`).

## Rules: per-frame update
9. The base update runs first. If it fails, the member's name is reported as a fatal problem
   (`staff_update.runs_the_base_update_first_and_ends_the_game_when_it_fails`).
10. A pending "directed" flag announces the matching thought once (add to the message log, mark, push the thought) and clears
    the flag (`staff_update.flag_thought_is_announced_once_and_clears_the_flag`).
11. A sacked member announces once for the whole staff: it is announced only while the staff-wide mask does not yet have the
    sacked bit; the member's own flag stays set (`staff_update.sacked_member_is_announced_once_for_the_whole_staff`).
12. Both announcements in one frame come in the order directed, sacked; an unrelated bit in the mask does not hide the sacked one
    (`staff_update.both_announcements_in_one_frame_come_in_order`).
13. Happiness moves by `rate * frame / hour_seconds` where the hour is the integer day length divided by 24 (240 s gives 10 s;
    0.5 per hour for 0.02 s gives +0.001) and a negative rate walks down. A day of 250 s equals one of 240 s because the hour is
    truncated (`staff_update.happiness_walks_by_rate_per_game_hour`, `staff_update.integer_hour_length_truncates_the_day_by_24`).
14. The walk ends (changing flag cleared, rate zeroed before the step, so happiness stays) when the target and happiness agree to
    the thousandth after truncation toward zero (0.5004 and 0.5009 agree; 0.5009 and 0.5011 do not; -0.0005 and +0.0005 agree). Without the
    changing flag nothing is reset (`staff_update.walk_ends_when_target_and_happiness_agree_to_the_thousandth`,
    `staff_update.thousandths_are_truncated_toward_zero`).
15. Happiness is held in 0..1 and never stored as negative zero (`staff_update.happiness_is_clamped_to_zero_and_one`).
16. Degenerate input: a day shorter than 24 s (hour of 0) sends a positive rate to 1, a negative rate to 0 and a zero rate to 0; a
    negative day reverses the walk; a happiness or rate that is not a number becomes 0, a target that is not a number counts as
    0 thousandths; a zero frame leaves the happiness (`staff_update.degenerate_frames_and_days_follow_the_x87_results`).
17. The step is computed in double precision and rounded to float once (`staff_update.step_uses_double_precision_and_stores_a_float_once`).
18. The profession dispatcher runs the base update, then the feedback update, then the profession's script update; a failed base
    update is reported and the rest still runs (`staff_update.dispatcher_runs_the_base_the_feedback_and_the_script_in_order`,
    `staff_update.dispatcher_reports_a_failed_base_update_and_goes_on`).
19. A builder additionally reallocates the builders when its shift start differs from the one remembered (including the
    initial "never seen" value -1), and remembers the new one. The start is read after the script ran
    (`staff_update.builder_reallocates_only_when_the_shift_start_moved`, `staff_update.builder_reads_the_shift_start_after_the_script`).

## Rules: feedback texts
20. The base keys, lowest set bit first: 0x400000 `FEEDBACK_STAFF_CANT_DO_JOB`, 0x200000 `FEEDBACK_STAFF_NEXT_JOB_I_DO`, 0x800000
    `FEEDBACK_STAFFTHOUGHT_UNHAPPY`, then `..._STARTWORK`, `..._ENDWORK`, `..._PAYRISE`, `..._PAYCUT` and `FEEDBACK_STAFF_SACKED`. No
    random number is drawn for a fixed key; no flag, or only profession bits, give no key
    (`staff_thoughts.base_fixed_keys_in_the_fixed_order`).
21. Directed picks `FEEDBACK_STAFF_DIRECTED_01..03` and overworked `FEEDBACK_STAFF_OVERWORKED_00..02` from one draw modulo 3; directed
    wins when both are set, with a single draw (`staff_thoughts.base_draws_directed_and_overworked_variants`).
22. A profession asks the base first; the base's draw is spent even when the profession then answers with a fixed key. A
    profession rule replaces the base key; a flag the profession has no rule for leaves the base answer
    (`staff_thoughts.profession_asks_the_base_first_and_its_draw_is_spent_even_when_it_answers_fixed`).
23. Per profession the rule order, with the first matching flag winning (`staff_thoughts.cleaner_order_and_variants`,
    `staff_thoughts.builder_order_and_variants`, `staff_thoughts.mechanic_and_security_order`,
    `staff_thoughts.rep_order_and_gendered_variants`, `staff_thoughts.lifeguard_order_and_gendered_variants`):
    - cleaner: 0x4 no toilets, 0x2 toilets busy, 0x100000 unhappy, then draw-based inner thoughts for 0x8 empty bin, 0x10 clean
      toilet, 0x40 clean litter, 0x20 go stroll, 0x20000000 stand idle; a draw of 0, 1, 2 gives suffix 01, 02, 03;
    - builder: 0x2 woken up, 0x100000 unhappy, 0x4 full quota, then the idle thought, then construct (0x8), demolish (0x10) and
      whistling (0x20) thoughts;
    - mechanic: 0x2 too busy, 0x100000 unhappy, then inspection made (0x4), building repair (0x8), scenery repair (0x10) and
      scenery watered (0x20), the last with no draw;
    - security: 0x2 no bins, 0x10 jail full, 0x100000 unhappy, 0x20 suspicious, 0x800 detain fighter, then draw-based idle;
    - rep: 0x2 beauty, sales bits (0x10000, before unhappy), 0x100000 unhappy, idle and patrolling (0x20000) thoughts that ask the
      gender once before the draw and use a male or female key; other branches never ask the gender;
    - lifeguard: 0x2 guests drowned, 0x20 is-OK, 0x100000 unhappy, 0x80 guest in trouble, rescued, then idle with a gendered key.
24. A draw out of range (negative) leaves the base answer (`staff_thoughts.a_draw_out_of_range_leaves_the_base_answer`).

## Rules: thought list
25. The member keeps up to three thoughts, newest first. The first push into an empty list asks for the text once and stores it
    (`staff_thoughts.push_into_an_empty_list`).
26. Every later push asks the text twice (once for the duplicate check, once for the real text) and stores the second answer,
    shifting older ones back and dropping the oldest at three (`staff_thoughts.push_shifts_the_older_thoughts_and_keeps_three`,
    `staff_thoughts.push_asks_twice_when_the_list_is_not_empty`, `staff_thoughts.push_at_three_drops_the_oldest`).
27. The duplicate check compares the address of the newest stored buffer, not the words: the same words in another buffer are
    pushed anyway, and an answer that is that very buffer is not pushed (`staff_thoughts.push_duplicate_guard_compares_addresses_and_fires_only_on_the_buffer_itself`).

## Not covered by tests
- What the entry and exit scripts do; where the can't-do-job flag and the offender are set.
- The effect of the announcements on the interface beyond the log calls.
- The staff-script accessor for a member's definition is a layout test only.

## Tests
`tests/unit/staff_state_test.cpp`, `tests/unit/staff_update_test.cpp`, `tests/unit/staff_thoughts_test.cpp`.
