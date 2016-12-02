
%https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4172289/


fs_Hz = 44100;      %sample rate
comp_ratio = 8;     %compression ratio.  "5" means 5:1
attack_sec = 0.005;  %doesn't end up being exact.  Error is dependent upon compression ratio
release_sec = 0.1;  %doesn't end up being exact.  Error is dependent upon compression ratio
thresh_dBFS = -15/(1-1/comp_ratio);


%generate test signal
t_sec = ([1:4*fs_Hz]-1)/fs_Hz;
freq_Hz = 2000;  %frequency of my test signal
wav = sqrt(2)*sin(2*pi*freq_Hz*t_sec);  %rms of 1.0 (aka 0 dB)

%make step change in amplitude
flag_isAttackTest = 0;  %do an attack or a release test?
if (flag_isAttackTest)
    levels_dBFS = [-25 0];%two amplitude levels
else
    levels_dBFS = [0 -25];  %two amplitude levels
end
t_bound_sec = 1;        %time of transition
I=find(t_sec < t_bound_sec);
wav(I) = sqrt(10.^(0.1*levels_dBFS(1)))*wav(I);
I=find(t_sec >= t_bound_sec);
wav(I) = sqrt(10.^(0.1*levels_dBFS(2)))*wav(I);

%% process the signal

%prepare constants
thresh_pow_FS = 10.^(0.1*thresh_dBFS);
attack_const = exp(-1/(attack_sec*fs_Hz));
release_const = exp(-1/(release_sec*fs_Hz));
comp_ratio_const = (1/comp_ratio-1);
thresh_pow_FS_wCR = thresh_pow_FS^(1-1/comp_ratio);

%get signal power
wav_pow = wav.^2;

%the next operations require log, which has no DSP acceleration,
%so let's start the sample-by-sample looping so that we only
%apply the expensive operations to the samples that need it
gain_pow = ones(size(wav_pow)); %default gain is 1.0
for I=2:length(wav_pow)
    %does it need to be compressed
    if (wav_pow(I) > thresh_pow_FS)
        %compute desired gain assuming that it needs to be compressed
        %gain_pow(I) = 10.^(comp_ratio_const*log10(wav_pow ./ thresh_pow_FS(I)));  %log and power
        gain_pow(I) = thresh_pow_FS_wCR./(wav_pow(I).^(1-1/comp_ratio)); %power
    end
    
    %smooth gain via attack and release and
    if (gain_pow(I) < gain_pow(I-1))
        %attack phase
        gain_pow(I) = gain_pow(I-1)*attack_const + gain_pow(I)*(1-attack_const);
    else
        %release phase
        gain_pow(I) = gain_pow(I-1)*release_const + gain_pow(I)*(1-release_const);
    end
end

%compute the gain from the gain_pow (the sqrt() has DSP acceleration available
gain = sqrt(gain_pow);

%apply the gain
new_wav = wav.*gain;

%% plots
figure;try;setFigureTallestWide;catch;end
ax=[];

subplot(4,2,1);
plot(t_sec,wav);
xlabel('Time (sec)');
ylabel('Raw Waveform');
ylim([-2 2]);
hold on; 
plot(xlim,sqrt(10.^(0.1*thresh_dBFS))*[1 1],'k--','linewidth',2);
plot(xlim,-sqrt(10.^(0.1*thresh_dBFS))*[1 1],'k--','linewidth',2);
hold off
ax(end+1)=gca;

subplot(4,2,2);
plot(t_sec,10*log10(wav_pow));
xlabel('Time (sec)');
ylabel({'Signal Power';'(dBFS)'});
ylim([-40 10]);
hold on; 
plot(xlim,thresh_dBFS*[1 1],'k--','linewidth',2);
hold off
ax(end+1)=gca;
% 
% subplot(4,2,3);
% plot(t_sec,10*log10(new_wav_pow));
% xlabel('Time (sec)');
% ylabel({'Smoothed Signal';'Power (dBFS)'});
% ylim([-40 10]);
% hold on; 
% plot(xlim,thresh_dBFS*[1 1],'k--','linewidth',2);
% hold off
% ax(end+1)=gca;


subplot(4,2,4);
plot(t_sec,10*log10(wav_pow_rel_thresh));
xlabel('Time (sec)');
ylabel({'Signal Power (dB)';'Rel Threshold'});
ylim([-20 20]);
hold on; 
plot(xlim,[0 0],'k--','linewidth',2);
hold off
ax(end+1)=gca;

subplot(4,2,5);
plot(t_sec,10*log10(gain.^2))
xlabel('Time (sec)');
ylabel({'Target Gain';'(dB)'});
ylim([-15 0]);
hold on; 
plot(xlim,[0 0],'k--','linewidth',2);
hold off
ax(end+1)=gca;

subplot(4,2,6);
plot(t_sec,new_wav);
xlabel('Time (sec)');
ylabel('New Waveform');
ylim([-2 2]);
hold on; 
plot(xlim,sqrt(10.^(0.1*thresh_dBFS))*[1 1],'k--','linewidth',2);
plot(xlim,-sqrt(10.^(0.1*thresh_dBFS))*[1 1],'k--','linewidth',2);
hold off
ax(end+1)=gca;




% subplot(3,2,5);
% plot(t_sec,10*log10(new_gain_pow));
% xlabel('Time (sec)');
% ylabel('Smoothed Gain (dB)');
% ylim([-10 0]);
% hold on; 
% plot(xlim,[0 0],'k--','linewidth',2);
% hold off

linkaxes(ax,'x');
xlim([0.9 1.2]);

subplot(4,2,7)
dt_sec = t_sec-t_bound_sec;
end_gain = mean(gain_pow(end+[-fs_Hz/4:0]));
start_gain = mean(gain_pow(fs_Hz/4:fs_Hz/2));
if (flag_isAttackTest)
    scaled_gain = (gain_pow-end_gain)/(start_gain-end_gain);
else
    scaled_gain = (gain_pow-start_gain)/(end_gain-start_gain);
end
semilogx(dt_sec,scaled_gain);
xlabel('Time (sec) Since Transition');
ylabel({'Scaled Gain'});
%x(end+1)=gca;
xlim([1e-4 1]);
ylim([0 1]);

if (flag_isAttackTest)
    time_const_val = 1-0.63;
    hold on;plot(xlim,time_const_val*[1 1],'k--','linewidth',2);hold off
    I=find(scaled_gain > time_const_val);I=I(end)+1;

    hold on;plot(dt_sec(I)*[1 1],ylim,'k:','linewidth',2);hold off;
    yl=ylim;
    text(dt_sec(I),yl(2)-0.05*diff(yl),[num2str(dt_sec(I),4) ' sec'], ...
        'horizontalAlignment','center','verticalAlignment','top',...
        'backgroundcolor','white');
else
    time_const_val = 0.63;
    hold on;plot(xlim,time_const_val*[1 1],'k--','linewidth',2);hold off
    I=find(scaled_gain < time_const_val);I=I(end)+1;
 
    hold on;plot(dt_sec(I)*[1 1],ylim,'k:','linewidth',2);hold off;
    yl=ylim;
    text(dt_sec(I),yl(1)+0.05*diff(yl),[num2str(dt_sec(I),4) ' sec'], ...
        'horizontalAlignment','center','verticalAlignment','bottom',...
        'backgroundcolor','white');
end
   

% 
% subplot(4,2,8);
% dt_sec = t_sec-t_bound_sec;
% gain_rel_final_dB = 10*log10(gain_pow/gain_pow(end));
% semilogx(dt_sec,gain_rel_final_dB)
% xlabel('Time (sec) Since Transition');
% ylabel({'Gain (dB) Re:';'Final Gain (dB)'});
% xlim([1e-4 1]);
% if (flag_isAttackTest)
%     ylim([-1 10]);
%     time_const_val = 2;
%     hold on;plot(xlim,time_const_val*[1 1],'k--','linewidth',2);hold off
%     I=find(gain_rel_final_dB > time_const_val);I=I(end)+1;
% 
%     hold on;plot(dt_sec(I)*[1 1],ylim,'k:','linewidth',2);hold off;
%     yl=ylim;
%     text(dt_sec(I),yl(2)-0.05*diff(yl),[num2str(dt_sec(I),4) ' sec'], ...
%         'horizontalAlignment','center','verticalAlignment','top',...
%         'backgroundcolor','white');
% else
%     ylim([-10 1]);
%     time_const_val = -2;
%     hold on;plot(xlim,time_const_val*[1 1],'k--','linewidth',2);hold off
%     I=find(gain_rel_final_dB < time_const_val);I=I(end)+1;
%  
%     hold on;plot(dt_sec(I)*[1 1],ylim,'k:','linewidth',2);hold off;
%     yl=ylim;
%     text(dt_sec(I),yl(1)+0.05*diff(yl),[num2str(dt_sec(I),4) ' sec'], ...
%         'horizontalAlignment','center','verticalAlignment','bottom',...
%         'backgroundcolor','white');
% end
   


