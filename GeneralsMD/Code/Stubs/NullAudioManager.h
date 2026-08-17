/*
 * NullAudioManager - the silent audio device.
 *
 * The shipping game plays sound through MilesAudioManager, which needs the
 * Miles Sound System SDK: binary-only, not part of the source release, and not
 * reconstructible from the headers.  So the exe gets an AudioManager that
 * answers every device question with "no provider, no channels, nothing
 * playing" and drops the audio the engine hands it.
 *
 * Everything above the device layer is the real thing - AudioManager's own code
 * still parses the INI files, tracks events and requests, and runs the music and
 * sound managers.  Only the part that would have reached a sound card is gone.
 *
 * Swapping in a real OpenAL/miniaudio device later means writing another
 * subclass and changing Win32GameEngine::createAudioManager; nothing else in the
 * game knows which device it got.
 */

#pragma once

#ifndef __NULLAUDIOMANAGER_H_
#define __NULLAUDIOMANAGER_H_

#include "Common/GameAudio.h"
#include "Common/AsciiString.h"

class NullAudioManager : public AudioManager
{
public:
	NullAudioManager() : m_speakerType( 0 ) {}
	virtual ~NullAudioManager() {}

#if defined(_DEBUG) || defined(_INTERNAL)
	virtual void audioDebugDisplay( DebugDisplayInterface *, void *, FILE * = NULL ) {}
#endif

	// stop/pause/resume: nothing is playing, so there is nothing to act on
	virtual void stopAudio( AudioAffect ) {}
	virtual void pauseAudio( AudioAffect ) {}
	virtual void resumeAudio( AudioAffect ) {}
	virtual void pauseAmbient( Bool ) {}

	virtual void killAudioEventImmediately( AudioHandle ) {}

	virtual void nextMusicTrack( void ) {}
	virtual void prevMusicTrack( void ) {}
	virtual Bool isMusicPlaying( void ) const { return FALSE; }
	/* The load screens wait on this one, so a silent device has to claim the
	   track finished or the wait never ends. */
	virtual Bool hasMusicTrackCompleted( const AsciiString&, Int ) const { return TRUE; }
	virtual AsciiString getMusicTrackName( void ) const { return AsciiString::TheEmptyString; }

	virtual void openDevice( void ) {}
	virtual void closeDevice( void ) {}
	virtual void *getDevice( void ) { return NULL; }

	virtual void notifyOfAudioCompletion( UnsignedInt, UnsignedInt ) {}

	/* One provider named "Software" keeps the options screen from showing an
	   empty list; selecting it is a no-op. */
	virtual UnsignedInt getProviderCount( void ) const { return 1; }
	virtual AsciiString getProviderName( UnsignedInt providerNum ) const
		{ return providerNum == 0 ? AsciiString( "Software" ) : AsciiString::TheEmptyString; }
	virtual UnsignedInt getProviderIndex( AsciiString ) const { return 0; }
	virtual void selectProvider( UnsignedInt ) {}
	virtual void unselectProvider( void ) {}
	virtual UnsignedInt getSelectedProvider( void ) const { return 0; }
	virtual void setSpeakerType( UnsignedInt speakerType ) { m_speakerType = speakerType; }
	virtual UnsignedInt getSpeakerType( void ) { return m_speakerType; }

	virtual UnsignedInt getNum2DSamples( void ) const { return 0; }
	virtual UnsignedInt getNum3DSamples( void ) const { return 0; }
	virtual UnsignedInt getNumStreams( void ) const { return 0; }

	/* No channels are in use, so no event can violate a limit or lose a
	   priority contest, and nothing is ever already playing. */
	virtual Bool doesViolateLimit( AudioEventRTS * ) const { return FALSE; }
	virtual Bool isPlayingLowerPriority( AudioEventRTS * ) const { return FALSE; }
	virtual Bool isPlayingAlready( AudioEventRTS * ) const { return FALSE; }
	virtual Bool isObjectPlayingVoice( UnsignedInt ) const { return FALSE; }

	virtual void adjustVolumeOfPlayingAudio( AsciiString, Real ) {}
	virtual void removePlayingAudio( AsciiString ) {}
	virtual void removeAllDisabledAudio() {}

	virtual Bool has3DSensitiveStreamsPlaying( void ) const { return FALSE; }

	/* Bink is stubbed out too, so nobody asks for this handle. */
	virtual void *getHandleForBink( void ) { return NULL; }
	virtual void releaseHandleForBink( void ) {}

	virtual void friend_forcePlayAudioEventRTS( const AudioEventRTS * ) {}

	virtual void setPreferredProvider( AsciiString ) {}
	virtual void setPreferredSpeaker( AsciiString ) {}

	/* Scripts time waits off this; a zero-length file just means the wait ends
	   on the next frame. */
	virtual Real getFileLengthMS( AsciiString ) const { return 0.0f; }

	virtual void closeAnySamplesUsingFile( const void * ) {}

protected:
	virtual void setDeviceListenerPosition( void ) {}

private:
	UnsignedInt m_speakerType;
};

#endif // __NULLAUDIOMANAGER_H_
