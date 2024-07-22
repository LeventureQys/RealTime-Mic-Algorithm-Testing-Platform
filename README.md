Once you want to modify the Audio Function Algorithm,go to /Audio.h

Find writeData and modify this line:

//here is where you put your algorithm
//float snr = df_process_frame_i16(df_state, input, output);

and modify how input and output works.
