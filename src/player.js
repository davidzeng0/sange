const EventEmitter = require('events');

const bindings = require('bindings');
const ffplayer = bindings('sange');

class Player extends EventEmitter{
	constructor(buffer, bind_emitters = true){
		super();

		this.paused = false;
		this.ffplayer = buffer ? new ffplayer(buffer) : new ffplayer();

		if(bind_emitters){
			this.ffplayer.onready = this.emit.bind(this, 'ready');
			this.ffplayer.onpacket = this.emit.bind(this, 'packet');
			this.ffplayer.onfinish = this.emit.bind(this, 'finish');
			this.ffplayer.ondebug = this.emit.bind(this, 'debug');
			this.ffplayer.onerror = this.emit.bind(this, 'onerror');
		}
	}

	setURL(url, isfile = false){
		return this.ffplayer.setURL(url, isfile);
	}

	setOutput(channels, sample_rate, bitrate){
		return this.ffplayer.setOutput(channels, sample_rate, bitrate);
	}

	isPaused(){
		return this.paused;
	}

	setPaused(paused){
		this.paused = paused;

		return this.ffplayer.setPaused(paused);
	}

	setVolume(volume){
		return this.ffplayer.setVolume(volume);
	}

	setBitrate(bitrate){
		return this.ffplayer.setBitrate(bitrate);
	}

	setRate(rate){
		return this.ffplayer.setRate(rate);
	}

	setTempo(tempo){
		return this.ffplayer.setTempo(tempo);
	}

	setTremolo(depth, rate){
		return this.ffplayer.setTremolo(depth, rate);
	}

	setEqualizer(eqs){
		return this.ffplayer.setEqualizer(...eqs);
	}

	seek(time){
		return this.ffplayer.seek(time);
	}

	getTime(){
		return this.ffplayer.getTime();
	}

	getDuration(){
		return this.ffplayer.getDuration();
	}

	getFramesDropped(){
		return this.ffplayer.getFramesDropped();
	}

	getTotalFrames(){
		return this.ffplayer.getTotalFrames();
	}

	start(){
		return this.ffplayer.start();
	}

	stop(){
		return this.ffplayer.stop();
	}

	destroy(){
		return this.ffplayer.destroy();
	}
}

module.exports = Player;