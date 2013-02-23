#include "e_mod_mixer.h"

Eina_Bool _mixer_using_default = EINA_FALSE;
E_Mixer_Volume_Get_Cb e_mod_mixer_volume_get;
E_Mixer_Volume_Set_Cb e_mod_mixer_volume_set;
E_Mixer_Mute_Get_Cb e_mod_mixer_mute_get;
E_Mixer_Mute_Set_Cb e_mod_mixer_mute_set;
E_Mixer_Capture_Cb e_mod_mixer_mutable_get;
E_Mixer_State_Get_Cb e_mod_mixer_state_get;
E_Mixer_Capture_Cb e_mod_mixer_capture_get;
E_Mixer_Cb e_mod_mixer_new;
E_Mixer_Cb e_mod_mixer_del;
E_Mixer_Cb e_mod_mixer_channel_default_name_get;
E_Mixer_Cb e_mod_mixer_channel_get_by_name;
E_Mixer_Cb e_mod_mixer_channel_name_get;
E_Mixer_Cb e_mod_mixer_channel_del;
E_Mixer_Cb e_mod_mixer_channel_free;
E_Mixer_Cb e_mod_mixer_channels_free;
E_Mixer_Cb e_mod_mixer_channels_get;
E_Mixer_Cb e_mod_mixer_channels_names_get;
E_Mixer_Cb e_mod_mixer_card_name_get;
E_Mixer_Cb e_mod_mixer_cards_get;
E_Mixer_Cb e_mod_mixer_cards_free;
E_Mixer_Cb e_mod_mixer_card_default_get;

void
e_mixer_default_setup(void)
{
   e_mod_mixer_volume_get = (void *)e_mixer_system_get_volume;
   e_mod_mixer_volume_set = (void *)e_mixer_system_set_volume;
   e_mod_mixer_mute_get = (void *)e_mixer_system_get_mute;
   e_mod_mixer_mute_set = (void *)e_mixer_system_set_mute;
   e_mod_mixer_mutable_get = (void *)e_mixer_system_can_mute;
   e_mod_mixer_state_get = (void *)e_mixer_system_get_state;
   e_mod_mixer_capture_get = (void *)e_mixer_system_has_capture;
   e_mod_mixer_new = (void *)e_mixer_system_new;
   e_mod_mixer_del = (void *)e_mixer_system_del;
   e_mod_mixer_channel_default_name_get = (void *)e_mixer_system_get_default_channel_name;
   e_mod_mixer_channel_get_by_name = (void *)e_mixer_system_get_channel_by_name;
   e_mod_mixer_channel_name_get = (void *)e_mixer_system_get_channel_name;
   e_mod_mixer_channel_del = (void *)e_mixer_system_channel_del;
   e_mod_mixer_channels_free = (void *)e_mixer_system_free_channels;
   e_mod_mixer_channels_get = (void *)e_mixer_system_get_channels;
   e_mod_mixer_channels_names_get = (void *)e_mixer_system_get_channels_names;
   e_mod_mixer_card_name_get = (void *)e_mixer_system_get_card_name;
   e_mod_mixer_cards_get = (void *)e_mixer_system_get_cards;
   e_mod_mixer_cards_free = (void *)e_mixer_system_free_cards;
   e_mod_mixer_card_default_get = (void *)e_mixer_system_get_default_card;
   _mixer_using_default = EINA_TRUE;
}

void
e_mixer_pulse_setup()
{
   e_mod_mixer_volume_get = (void *)e_mixer_pulse_get_volume;
   e_mod_mixer_volume_set = (void *)e_mixer_pulse_set_volume;
   e_mod_mixer_mute_get = (void *)e_mixer_pulse_get_mute;
   e_mod_mixer_mute_set = (void *)e_mixer_pulse_set_mute;
   e_mod_mixer_mutable_get = (void *)e_mixer_pulse_can_mute;
   e_mod_mixer_state_get = (void *)e_mixer_pulse_get_state;
   e_mod_mixer_capture_get = (void *)e_mixer_pulse_has_capture;
   e_mod_mixer_new = (void *)e_mixer_pulse_new;
   e_mod_mixer_del = (void *)e_mixer_pulse_del;
   e_mod_mixer_channel_default_name_get = (void *)e_mixer_pulse_get_default_channel_name;
   e_mod_mixer_channel_get_by_name = (void *)e_mixer_pulse_get_channel_by_name;
   e_mod_mixer_channel_name_get = (void *)e_mixer_pulse_get_channel_name;
   e_mod_mixer_channel_del = (void *)e_mixer_pulse_channel_del;
   e_mod_mixer_channels_free = (void *)e_mixer_pulse_free_channels;
   e_mod_mixer_channels_get = (void *)e_mixer_pulse_get_channels;
   e_mod_mixer_channels_names_get = (void *)e_mixer_pulse_get_channels_names;
   e_mod_mixer_card_name_get = (void *)e_mixer_pulse_get_card_name;
   e_mod_mixer_cards_get = (void *)e_mixer_pulse_get_cards;
   e_mod_mixer_cards_free = (void *)e_mixer_pulse_free_cards;
   e_mod_mixer_card_default_get = (void *)e_mixer_pulse_get_default_card;
   _mixer_using_default = EINA_FALSE;
}

