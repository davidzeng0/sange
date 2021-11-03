## player

Internal API

### player.js

```js
class Player extends EventEmitter
```

Instantiate a player
```js
const {AudioPlayer} = require('sange');

var player = new AudioPlayer();

player.on('ready', () => { console.log('ready') });
player.on('packet', (p) => { console.log('packet', p) });
player.on('finish', () => { console.log('finish') });
player.on('error', (e, r) => { console.log('error', e) });

```

Play from a URL
```js
player.setURL('http://example.com/video.mp4');
player.setOutput(2, 48000, 256000); // 2 channels, 48000Hz, 256kbps
player.start();
```

#### Events

Ready
```js
player.on('ready', () => {
	console.log(`Ready! Duration: ${player.getDuration()} seconds`);
});
```

Packet
```js
class Packet{
	frame_size: number; // number of samples
	buffer: Buffer; // frame data
}

player.on('packet', (packet: Packet) => {
	console.log(`Packet: ${packet.frame_size} samples`);
});
```

Finish
```js
player.on('finish', () => {
	console.log('Finished playing the track');

	// play again
	player.seek(0);
});
```

Error
```js
player.on('error', (error: Error, code: number, retryable: boolean) => {
	console.log(`Error: ${error.message}`);

	if(retryable && shouldStillRetry()){ // dont retry too many times if it fails every time
		// retryable is true when there is an http error
		// many platforms have expiring stream links
		var time = player.getTime();

		player.stop();
		player.setURL(new_stream_url);
		player.seek(time);
		player.start();
	}else{
		// cleanup
		player.destroy();
	}
});
```

#### Member Functions

Set the URL source of the player
```js
player.setURL(url: string): void
```

Set the output format
```js
player.setOutput(channels: number, sampleRate: number, bitRate: number): void
```

Pause or unpause the player
```js
player.setPaused(paused: boolean): void
```

Check if the player is paused
```js
player.isPaused(): boolean
```

Set the player's volume
```js
// 1 = 100%
// 1.5 = 150%
// 0.5 = 50%
player.setVolume(volume: number): void
```

Set the player's bitrate
```js
//bitrate in bits/sec
player.setBitrate(bitrate: number): void
```

Set the player's speed
```js
// 1 = normal speed
// 2 = double speed
// 0.5 = half speed
player.setRate(rate: number): void
```

Set the player's tempo
```js
// 1 = normal tempo
// 2 = double tempo
// 0.5 = half tempo
player.setTempo(tempo: number): void
```

Set a tremolo effect
```js
player.setTremolo(depth: number, rate: number): void

// stop tremolo
player.setTremolo(0, 0);
```

Set an equalizer
```js
class EqualizerSetting{
	band: number, // Hz
	gain: number // dB
}

player.setEqualizer(eqs: EqualizerSetting[]): void

// stop equalizer
player.setEqualizer([]);
```

Seek the player (can be called anytime)
```js
// time in seconds
player.seek(time: number): void
```

Get player's current time
```js
// time in seconds
player.getTime(): number
```

Get player's duration
```js
// time in seconds
player.getDuration(): number
```

Start the player
```js
player.start(): void
```

Stop the player
```js
player.stop(): void
```

Destroy the player and free internal resources
```js
player.destroy(): void
```