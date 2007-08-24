/*****************************************************************************
 * services_discovery.c : Manage playlist services_discovery modules
 *****************************************************************************
 * Copyright (C) 1999-2004 the VideoLAN team
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#include <vlc/vlc.h>
#include "vlc_playlist.h"
#include "vlc_events.h"
#include "playlist_internal.h"

static void RunSD( services_discovery_t *p_sd );

/*
 * Services discovery
 * Basically you just listen to Service discovery event through the
 * sd's event manager.
 * That's how the playlist get's Service Discovery information
 */

/***********************************************************************
 * GetServicesNames
 ***********************************************************************/
char ** services_discovery_GetServicesNames( vlc_object_t * p_super )
{
    return module_GetModulesNamesForCapability( p_super, "services_discovery" );
}

/***********************************************************************
 * Create
 ***********************************************************************/
services_discovery_t *
services_discovery_Create ( vlc_object_t * p_super, const char * psz_module_name )
{
    services_discovery_t *p_sd = vlc_object_create( p_super, VLC_OBJECT_SD );
    if( !p_sd )
        return NULL;
    
    p_sd->pf_run = NULL;
    p_sd->psz_localized_name = NULL;

    vlc_event_manager_init( &p_sd->event_manager, p_sd, (vlc_object_t *)p_sd );
    vlc_event_manager_register_event_type( &p_sd->event_manager, 
            vlc_ServicesDiscoveryItemAdded );
    vlc_event_manager_register_event_type( &p_sd->event_manager, 
            vlc_ServicesDiscoveryItemRemoved );

    p_sd->p_module = module_Need( p_sd, "services_discovery", psz_module_name, 0 );
    
    if( p_sd->p_module == NULL )
    {
        msg_Err( p_super, "no suitable services discovery module" );
        vlc_object_destroy( p_sd );
        return NULL;
    }
    p_sd->psz_module = strdup( psz_module_name );
    p_sd->b_die = VLC_FALSE; /* FIXME */

    return p_sd;
}

/***********************************************************************
 * Destroy
 ***********************************************************************/
void services_discovery_Destroy ( services_discovery_t * p_sd )
{
    vlc_event_manager_fini( &p_sd->event_manager );

    free( p_sd->psz_module );
    free( p_sd->psz_localized_name );

    vlc_object_destroy( p_sd );
}

/***********************************************************************
 * Start
 ***********************************************************************/
int services_discovery_Start ( services_discovery_t * p_sd )
{
    if ((p_sd->pf_run != NULL)
        && vlc_thread_create( p_sd, "services_discovery", RunSD,
                              VLC_THREAD_PRIORITY_LOW, VLC_FALSE))
    {
        msg_Err( p_sd, "cannot create services discovery thread" );
        vlc_object_destroy( p_sd );
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

/***********************************************************************
 * Stop
 ***********************************************************************/
void services_discovery_Stop ( services_discovery_t * p_sd )
{
    vlc_object_kill( p_sd );
    if( p_sd->pf_run ) vlc_thread_join( p_sd );

    module_Unneed( p_sd, p_sd->p_module );
}

/***********************************************************************
 * GetLocalizedName
 ***********************************************************************/
char *
services_discovery_GetLocalizedName ( services_discovery_t * p_sd )
{
    return p_sd->psz_localized_name ? strdup( p_sd->psz_localized_name ) : NULL;
}

/***********************************************************************
 * SetLocalizedName
 ***********************************************************************/
void
services_discovery_SetLocalizedName ( services_discovery_t * p_sd, const char *psz )
{
    if(p_sd->psz_localized_name) free( p_sd->psz_localized_name );
    p_sd->psz_localized_name = strdup(psz);
}

/***********************************************************************
 * EventManager
 ***********************************************************************/
vlc_event_manager_t *
services_discovery_EventManager ( services_discovery_t * p_sd )
{
    return &p_sd->event_manager;
}

/***********************************************************************
 * AddItem
 ***********************************************************************/
void
services_discovery_AddItem ( services_discovery_t * p_sd, input_item_t * p_item,
                             const char * psz_category )
{
    vlc_event_t event;
    event.type = vlc_ServicesDiscoveryItemAdded;
    event.u.services_discovery_item_added.p_new_item = p_item;
    event.u.services_discovery_item_added.psz_category = psz_category;

    vlc_event_send( &p_sd->event_manager, &event );
}

/***********************************************************************
 * RemoveItem
 ***********************************************************************/
void
services_discovery_RemoveItem ( services_discovery_t * p_sd, input_item_t * p_item )
{
    vlc_event_t event;
    event.type = vlc_ServicesDiscoveryItemRemoved;
    event.u.services_discovery_item_removed.p_item = p_item;

    vlc_event_send( &p_sd->event_manager, &event );
}

/***********************************************************************
 * RunSD (Private)
 ***********************************************************************/
static void RunSD( services_discovery_t *p_sd )
{
    p_sd->pf_run( p_sd );
    return;
}

/*
 * Playlist - Services discovery bridge
 */

 /* A new item has been added to a certain sd */
static void playlist_sd_item_added( const vlc_event_t * p_event, void * user_data )
{
    input_item_t * p_input = p_event->u.services_discovery_item_added.p_new_item;
    const char * psz_cat = p_event->u.services_discovery_item_added.psz_category;
    playlist_item_t *p_new_item, * p_parent = user_data;

    msg_Dbg( p_parent->p_playlist, "Adding %s in %s",
                p_input->psz_name ? p_input->psz_name : "(null)",
                psz_cat ? psz_cat : "(null)" );

    /* If p_parent is in root category (this is clearly a hack) and we have a cat */
    if( !EMPTY_STR(psz_cat) &&
        p_parent->p_parent == p_parent->p_playlist->p_root_category )
    {
        /* */
        playlist_item_t * p_cat;
        p_cat = playlist_ChildSearchName( p_parent, psz_cat );
        if( !p_cat )
        {
            p_cat = playlist_NodeCreate( p_parent->p_playlist, psz_cat,
                                         p_parent, 0 );
            p_cat->i_flags &= ~PLAYLIST_SKIP_FLAG;
        }
        p_parent = p_cat;
    }
        
    p_new_item = playlist_NodeAddInput( p_parent->p_playlist, p_input, p_parent,
                                        PLAYLIST_APPEND, PLAYLIST_END, VLC_FALSE );
    p_new_item->i_flags &= ~PLAYLIST_SKIP_FLAG;
    p_new_item->i_flags &= ~PLAYLIST_SAVE_FLAG;
}

 /* A new item has been removed from a certain sd */
static void playlist_sd_item_removed( const vlc_event_t * p_event, void * user_data )
{
    input_item_t * p_input = p_event->u.services_discovery_item_removed.p_item;
    playlist_item_t * p_parent = user_data;

    playlist_DeleteInputInParent( p_parent->p_playlist, p_input->i_id,
                                  p_parent, VLC_FALSE );
}

int playlist_ServicesDiscoveryAdd( playlist_t *p_playlist,  const char *psz_modules )
{
    const char *psz_parser = psz_modules ?: "";
    int retval = VLC_SUCCESS;

    for (;;)
    {
        struct playlist_archived_services_discovery_t * p_asd;
        playlist_item_t * p_cat;
        playlist_item_t * p_one;

        while( *psz_parser == ' ' || *psz_parser == ':' || *psz_parser == ',' )
            psz_parser++;

        if( *psz_parser == '\0' )
            break;

        const char *psz_next = strchr( psz_parser, ':' );
        if( psz_next == NULL )
            psz_next = psz_parser + strlen( psz_parser );

        char psz_plugin[psz_next - psz_parser + 1];
        memcpy (psz_plugin, psz_parser, sizeof (psz_plugin) - 1);
        psz_plugin[sizeof (psz_plugin) - 1] = '\0';
        psz_parser = psz_next;

        /* Perform the addition */
        msg_Dbg( p_playlist, "Add services_discovery %s", psz_plugin );
        services_discovery_t *p_sd;

        p_sd = services_discovery_Create( (vlc_object_t*)p_playlist, psz_plugin );
        if( !p_sd )
            continue;

        char * psz = services_discovery_GetLocalizedName( p_sd );
        if( psz )
        {
            playlist_NodesPairCreate( p_playlist, psz,
                    &p_cat, &p_one, VLC_FALSE );
            free( psz );
        }
        else
        {
            /* No name, just add at the top of the playlist */
            PL_LOCK;
            p_cat = p_playlist->p_root_category;
            p_one = p_playlist->p_root_onelevel;
            PL_UNLOCK;
        }

        vlc_event_attach( services_discovery_EventManager( p_sd ),
                          vlc_ServicesDiscoveryItemAdded,
                          playlist_sd_item_added,
                          p_one );

        vlc_event_attach( services_discovery_EventManager( p_sd ),
                          vlc_ServicesDiscoveryItemAdded,
                          playlist_sd_item_added,
                          p_cat );

        vlc_event_attach( services_discovery_EventManager( p_sd ),
                          vlc_ServicesDiscoveryItemRemoved,
                          playlist_sd_item_removed,
                          p_one );

        vlc_event_attach( services_discovery_EventManager( p_sd ),
                          vlc_ServicesDiscoveryItemRemoved,
                          playlist_sd_item_removed,
                          p_cat );

        services_discovery_Start( p_sd );

        /* Free in playlist_ServicesDiscoveryRemove */
        p_asd = malloc( sizeof(struct playlist_archived_services_discovery_t) );
        p_asd->p_sd = p_sd;
        p_asd->p_one = p_one;
        p_asd->p_cat = p_cat;
        
        PL_LOCK;
        TAB_APPEND( p_playlist->p_internal->i_asds, p_playlist->p_internal->pp_asds, p_asd );
        PL_UNLOCK;
    }

    return retval;
}

int playlist_ServicesDiscoveryRemove( playlist_t * p_playlist,
                                       const char *psz_module )
{
    int i;
    struct playlist_archived_services_discovery_t *p_asd = NULL;

    PL_LOCK;
    for( i = 0 ; i< p_playlist->p_internal->i_asds ; i ++ )
    {
        if( !strcmp( psz_module, p_playlist->p_internal->pp_asds[i]->p_sd->psz_module ) )
        {
            p_asd = p_playlist->p_internal->pp_asds[i];
            REMOVE_ELEM( p_playlist->p_internal->pp_asds, p_playlist->p_internal->i_asds, i );
            break;
        }
    }
    PL_UNLOCK;

    if( p_asd && p_asd->p_sd )
    {
        services_discovery_Stop( p_asd->p_sd );

        vlc_event_detach( services_discovery_EventManager( p_asd->p_sd ),
                          vlc_ServicesDiscoveryItemAdded,
                          playlist_sd_item_added,
                          p_asd->p_one );
        
        vlc_event_detach( services_discovery_EventManager( p_asd->p_sd ),
                          vlc_ServicesDiscoveryItemAdded,
                          playlist_sd_item_added,
                          p_asd->p_cat );
        
        vlc_event_detach( services_discovery_EventManager( p_asd->p_sd ),
                          vlc_ServicesDiscoveryItemRemoved,
                          playlist_sd_item_removed,
                          p_asd->p_one );
        
        vlc_event_detach( services_discovery_EventManager( p_asd->p_sd ),
                          vlc_ServicesDiscoveryItemRemoved,
                          playlist_sd_item_removed,
                          p_asd->p_cat );
        
        /* Remove the sd playlist node if it exists */
        PL_LOCK;
        if( p_asd->p_cat != p_playlist->p_root_category && 
            p_asd->p_one != p_playlist->p_root_onelevel )
        {
            playlist_NodeDelete( p_playlist, p_asd->p_cat, VLC_TRUE, VLC_FALSE );
            playlist_NodeDelete( p_playlist, p_asd->p_one, VLC_TRUE, VLC_FALSE );
        }
        PL_UNLOCK;

        services_discovery_Destroy( p_asd->p_sd );

        free( p_asd );
    }
    else
    {
        msg_Warn( p_playlist, "module %s is not loaded", psz_module );
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

vlc_bool_t playlist_IsServicesDiscoveryLoaded( playlist_t * p_playlist,
                                              const char *psz_module )
{
    int i;
    PL_LOCK;

    for( i = 0 ; i< p_playlist->p_internal->i_asds ; i ++ )
    {
        if( !strcmp( psz_module, p_playlist->p_internal->pp_asds[i]->p_sd->psz_module ) )
        {
            PL_UNLOCK;
            return VLC_TRUE;
        }
    }
    PL_UNLOCK;
    return VLC_FALSE;
}

